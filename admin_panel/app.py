
import os
import redis
import uuid
from flask import Flask, render_template, request, redirect, url_for, session, flash
from datetime import timedelta

# --- App Configuration ---
app = Flask(__name__)
app.config['SECRET_KEY'] = os.urandom(24)
app.config['PERMANENT_SESSION_LIFETIME'] = timedelta(minutes=30)

# --- Redis Connection ---
# Connect to the same Redis instance as the C server
r = redis.Redis(host='localhost', port=6379, db=0, decode_responses=True)

# --- Helper Functions ---

def get_admin_password():
    """Reads the admin password from the file."""
    try:
        with open('admin_password.txt', 'r') as f:
            return f.read().strip()
    except FileNotFoundError:
        return "password" # Default fallback

def is_logged_in():
    """Check if the user is logged in."""
    return session.get('logged_in', False)

def format_ttl(seconds):
    """Formats TTL in seconds into a human-readable string."""
    if seconds < 0:
        return "Expired/No TTL"
    days, remainder = divmod(seconds, 86400)
    hours, remainder = divmod(remainder, 3600)
    minutes, _ = divmod(remainder, 60)
    return f"{int(days)}d {int(hours)}h {int(minutes)}m"

# --- Routes ---

@app.route('/login', methods=['GET', 'POST'])
def login():
    if request.method == 'POST':
        password = request.form.get('password')
        if password == get_admin_password():
            session['logged_in'] = True
            session.permanent = True
            flash("Login successful!", "success")
            return redirect(url_for('index'))
        else:
            return render_template('login.html', error="Invalid password.")
    return render_template('login.html')

@app.route('/logout')
def logout():
    session.clear()
    return redirect(url_for('login'))

@app.route('/')
def index():
    if not is_logged_in():
        return redirect(url_for('login'))

    users = []
    search_query = request.args.get('search', '').strip()

    if search_query:
        # Search for a specific UUID
        keys = r.keys(f"user:{search_query}*")
    else:
        # Get all user keys
        keys = r.keys('user:*')

    for key in keys:
        user_data = r.hgetall(key)
        ttl = r.ttl(key)
        users.append({
            'uuid': key.split(':', 1)[1],
            'description': user_data.get('description', 'N/A'),
            'expires_in': format_ttl(ttl)
        })
    
    # Sort users by description
    users.sort(key=lambda x: x['description'])
    return render_template('index.html', users=users)

@app.route('/add', methods=['POST'])
def add_user():
    if not is_logged_in():
        return redirect(url_for('login'))

    description = request.form.get('description')
    duration_days = int(request.form.get('duration', 30))
    duration_seconds = duration_days * 86400

    new_uuid = str(uuid.uuid4())
    key = f"user:{new_uuid}"

    # Use a pipeline for atomic operations
    pipe = r.pipeline()
    pipe.hset(key, 'description', description)
    pipe.expire(key, duration_seconds)
    pipe.execute()

    flash(f"Successfully created user: {new_uuid}", "success")
    return redirect(url_for('index'))

@app.route('/delete/<uuid>', methods=['POST'])
def delete_user(uuid):
    if not is_logged_in():
        return redirect(url_for('login'))
    
    key = f"user:{uuid}"
    if r.exists(key):
        r.delete(key)
        flash(f"User {uuid} has been deleted.", "success")
    else:
        flash(f"User {uuid} not found.", "error")

    return redirect(url_for('index'))

if __name__ == '__main__':
    print("Starting admin panel...")
    print("WARNING: This is a development server.")
    print(f"Admin password is: {get_admin_password()}")
    # For production, use a proper WSGI server like Gunicorn or Waitress.
    # Example: waitress-serve --host 127.0.0.1 --port 8080 app:app
    app.run(host='127.0.0.1', port=8080, debug=True)
