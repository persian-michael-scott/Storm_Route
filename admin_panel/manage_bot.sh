#!/bin/bash

# --- Configuration ---
SERVICE_NAME="telegram-bot.service"
SERVICE_FILE="/etc/systemd/system/$SERVICE_NAME"
# Get the directory where the script is located to find other project files
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"

# --- Helper Functions ---
check_root() {
    if [ "$EUID" -ne 0 ]; then
        echo "This option requires root privileges. Please run the script with sudo."
        exit 1
    fi
}

# --- Main Functions ---

install_bot() {
    echo "--- Starting Bot Installation ---"
    check_root

    # 1. Install dependencies (git, python3, venv)
    echo "Updating package list and installing dependencies..."
    apt-get update > /dev/null
    apt-get install -y git python3-full python3-venv > /dev/null
    echo "Dependencies installed."

    # 2. Create Python virtual environment and install packages
    echo "Setting up Python virtual environment..."
    cd "$SCRIPT_DIR"
    python3 -m venv venv
    source venv/bin/activate
    pip install -r requirements.txt
    deactivate
    echo "Python environment is ready."

    # 3. Get credentials from user
    read -p "Please enter your TELEGRAM_TOKEN: " TELEGRAM_TOKEN
    read -p "Please enter your ADMIN_USER_ID: " ADMIN_USER_ID

    # 4. Create .env file
    echo "Creating .env configuration file..."
    cat > "$SCRIPT_DIR/.env" << EOF
TELEGRAM_TOKEN=$TELEGRAM_TOKEN
ADMIN_USER_ID=$ADMIN_USER_ID
EOF

    # 5. Create systemd service file
    echo "Creating systemd service file..."
    cat > "$SERVICE_FILE" << EOF
[Unit]
Description=Telegram Admin Bot for Storm_Route
After=network.target

[Service]
User=$(logname)
Group=$(logname)
WorkingDirectory=$SCRIPT_DIR
ExecStart=$SCRIPT_DIR/venv/bin/python $SCRIPT_DIR/telegram_bot.py
Restart=always
RestartSec=5s

[Install]
WantedBy=multi-user.target
EOF

    # 6. Enable and start the service
    echo "Enabling and starting the service..."
    systemctl daemon-reload
    systemctl enable "$SERVICE_NAME"
    systemctl start "$SERVICE_NAME"

    echo ""
    echo "--- Installation Complete! ---"
    echo "The bot is now running as a service."
    echo "You can check its status with: sudo systemctl status $SERVICE_NAME"
}

stop_bot() {
    echo "--- Stopping Bot Service ---"
    check_root
    systemctl stop "$SERVICE_NAME"
    echo "Service stopped."
}

delete_service() {
    echo "--- Stopping and Deleting Bot Service ---"
    check_root
    
    echo "Stopping the service..."
    systemctl stop "$SERVICE_NAME"
    
    echo "Disabling the service from starting on boot..."
    systemctl disable "$SERVICE_NAME"
    
    echo "Removing the service file..."
    rm -f "$SERVICE_FILE"
    
    echo "Reloading systemd daemon..."
    systemctl daemon-reload
    
    echo "Service has been stopped and the systemd file has been deleted."
    echo "Note: The bot files (scripts, .env, venv) have not been deleted."
}

# --- Menu Display ---
show_menu() {
    echo ""
    echo "================================="
    echo "  Telegram Bot Manager"
    echo "================================="
    echo "1. Install and Start Bot"
    echo "2. Stop Bot"
    echo "3. Stop and Delete Service"
    echo "4. Exit"
    echo "---------------------------------"
}

# --- Main Loop ---
while true; do
    show_menu
    read -p "Please select an option [1-4]: " choice
    case $choice in
        1)
            install_bot
            ;;
        2)
            stop_bot
            ;;
        3)
            delete_service
            ;;
        4)
            echo "Exiting."
            exit 0
            ;;
        *)
            echo "Invalid option. Please try again."
            ;;
    esac
done
