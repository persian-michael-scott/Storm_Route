import os
import redis
import uuid
import json
import logging
from datetime import datetime, timedelta
from telegram import Update, InlineKeyboardButton, InlineKeyboardMarkup, InputFile
from telegram.ext import (
    Application,
    CommandHandler,
    MessageHandler,
    filters,
    CallbackContext,
    CallbackQueryHandler,
    ConversationHandler,
)
from dotenv import load_dotenv
from apscheduler.schedulers.asyncio import AsyncIOScheduler

# --- Basic Setup ---
logging.basicConfig(format='%(asctime)s - %(name)s - %(levelname)s - %(message)s', level=logging.INFO)
logger = logging.getLogger(__name__)

# --- Load Configuration ---
load_dotenv()
TELEGRAM_TOKEN = os.getenv('TELEGRAM_TOKEN')
ADMIN_USER_ID = os.getenv('ADMIN_USER_ID')

if not TELEGRAM_TOKEN or not ADMIN_USER_ID:
    raise ValueError("TELEGRAM_TOKEN and ADMIN_USER_ID must be set in the .env file.")
try:
    ADMIN_USER_ID = int(ADMIN_USER_ID)
except ValueError:
    raise ValueError("ADMIN_USER_ID must be an integer.")

# --- Redis Connection ---
r = redis.Redis(host='localhost', port=6379, db=0, decode_responses=True)

# --- Conversation States ---
GET_DESCRIPTION, GET_DURATION, GET_BACKUP_FILE = range(3)

# --- Helper Functions ---
def format_ttl(seconds):
    if seconds < 0: return "Expired/No TTL"
    days, rem = divmod(seconds, 86400)
    hours, rem = divmod(rem, 3600)
    minutes, _ = divmod(rem, 60)
    return f"{int(days)}d {int(hours)}h {int(minutes)}m"

async def get_main_menu_keyboard():
    return InlineKeyboardMarkup([
        [InlineKeyboardButton("Create New User", callback_data='create_user_start')],
        [InlineKeyboardButton("Restore from Backup", callback_data='restore_backup_start')]
    ])

# --- Main Command Handlers ---
async def start(update: Update, context: CallbackContext) -> None:
    if update.effective_user.id != ADMIN_USER_ID:
        await update.message.reply_text("You are not authorized.")
        return
    
    reply_markup = await get_main_menu_keyboard()
    welcome_text = "Welcome, Admin! Select an option below or send a UUID to check a user."
    
    if update.callback_query:
        await update.callback_query.answer()
        await update.callback_query.edit_message_text(welcome_text, reply_markup=reply_markup)
    else:
        await update.message.reply_text(welcome_text, reply_markup=reply_markup)

# --- Backup and Restore ---
async def backup_users(context: CallbackContext):
    logger.info("Starting scheduled user backup...")
    users_to_backup = []
    for key in r.scan_iter("user:*"):
        user_data = r.hgetall(key)
        ttl = r.ttl(key)
        users_to_backup.append({
            'uuid': key.split(':', 1)[1],
            'description': user_data.get('description', 'N/A'),
            'ttl': ttl
        })

    if not users_to_backup:
        logger.info("No users to back up.")
        return

    filename = f"backup-{datetime.now().strftime('%Y-%m-%d_%H-%M-%S')}.json"
    with open(filename, 'w') as f:
        json.dump(users_to_backup, f, indent=4)

    try:
        with open(filename, 'rb') as f:
            await context.bot.send_document(ADMIN_USER_ID, document=InputFile(f), caption="Here is your scheduled user backup.")
        logger.info(f"Backup file {filename} sent to admin.")
    except Exception as e:
        logger.error(f"Failed to send backup file: {e}")
    finally:
        os.remove(filename)

async def restore_backup_start(update: Update, context: CallbackContext) -> int:
    query = update.callback_query
    await query.answer()
    keyboard = [[InlineKeyboardButton("Back to Main Menu", callback_data='start')]]
    await query.message.edit_text("Please upload the backup file (.json) to restore users.", reply_markup=InlineKeyboardMarkup(keyboard))
    return GET_BACKUP_FILE

async def restore_from_file(update: Update, context: CallbackContext) -> int:
    try:
        document = update.message.document
        if not document or not document.file_name.endswith('.json'):
            await update.message.reply_text("Please upload a valid .json backup file.")
            return GET_BACKUP_FILE

        file = await document.get_file()
        file_content = (await file.download_as_bytearray()).decode('utf-8')
        users_to_restore = json.loads(file_content)

        restored_count = 0
        pipe = r.pipeline()
        for user in users_to_restore:
            key = f"user:{user['uuid']}"
            pipe.hset(key, 'description', user['description'])
            if user['ttl'] > 0:
                pipe.expire(key, int(user['ttl']))
            restored_count += 1
        pipe.execute()

        await update.message.reply_text(f"Successfully restored {restored_count} users from the backup file.")
    except Exception as e:
        logger.error(f"Error restoring from file: {e}")
        await update.message.reply_text("An error occurred during the restore process.")
    
    await start(update, context) # Return to main menu
    return ConversationHandler.END

# --- User Creation Conversation ---
async def create_user_start(update: Update, context: CallbackContext) -> int:
    query = update.callback_query
    await query.answer()
    keyboard = [[InlineKeyboardButton("Back to Main Menu", callback_data='start')]]
    await query.message.edit_text("Please send a description for the new user.", reply_markup=InlineKeyboardMarkup(keyboard))
    return GET_DESCRIPTION

async def get_description(update: Update, context: CallbackContext) -> int:
    context.user_data['description'] = update.message.text
    keyboard = [[InlineKeyboardButton("Back to Main Menu", callback_data='start')]]
    await update.message.reply_text("Great. Now, for how many days should this user be valid?", reply_markup=InlineKeyboardMarkup(keyboard))
    return GET_DURATION

async def get_duration(update: Update, context: CallbackContext) -> int:
    try:
        duration_days = int(update.message.text)
        description = context.user_data['description']
        new_uuid = str(uuid.uuid4())
        key = f"user:{new_uuid}"

        r.hset(key, 'description', description)
        r.expire(key, duration_days * 86400)

        await update.message.reply_text(
            f"User created successfully!\n\n**UUID:** `{new_uuid}`\n**Description:** {description}",
            parse_mode='Markdown'
        )
    except (ValueError):
        await update.message.reply_text("That's not a valid number. Please try again.")
        return GET_DURATION
    except Exception as e:
        logger.error(f"Error in get_duration: {e}")
        await update.message.reply_text("An error occurred.")
    finally:
        context.user_data.clear()
    
    await start(update, context)
    return ConversationHandler.END

# --- UUID and Button Handling ---
async def handle_uuid_message(update: Update, context: CallbackContext) -> None:
    message_text = update.message.text.strip()
    try:
        uuid.UUID(message_text, version=4)
        key = f"user:{message_text}"
        if r.exists(key):
            user_data = r.hgetall(key)
            ttl = r.ttl(key)
            keyboard = [
                [InlineKeyboardButton("Extend 30 Days", callback_data=f"extend_{message_text}_30")],
                [InlineKeyboardButton("Delete User", callback_data=f"delete_{message_text}")],
                [InlineKeyboardButton("Back to Main Menu", callback_data='start')]
            ]
            await update.message.reply_text(
                f"**User Details**\nUUID: `{message_text}`\nDescription: {user_data.get('description', 'N/A')}\nExpires in: {format_ttl(ttl)}",
                reply_markup=InlineKeyboardMarkup(keyboard), parse_mode='Markdown'
            )
        else:
            await update.message.reply_text("User with this UUID not found.")
    except ValueError:
        await update.message.reply_text("Invalid UUID. Please send a valid UUID or use the main menu.")

async def manage_user_button_handler(update: Update, context: CallbackContext) -> None:
    query = update.callback_query
    await query.answer()
    action, user_uuid, *params = query.data.split('_')
    key = f"user:{user_uuid}"

    if not r.exists(key):
        await query.edit_message_text("This user no longer exists.")
        return

    if action == "extend":
        days = int(params[0])
        new_ttl = r.ttl(key) + (days * 86400)
        r.expire(key, new_ttl)
        await query.edit_message_text(f"User `{user_uuid}` extended by {days} days.", parse_mode='Markdown')
    elif action == "delete":
        r.delete(key)
        await query.edit_message_text(f"User `{user_uuid}` has been deleted.", parse_mode='Markdown')
    
    await start(update, context)

async def cancel_conversation(update: Update, context: CallbackContext) -> int:
    await update.message.reply_text("Operation canceled.")
    context.user_data.clear()
    await start(update, context)
    return ConversationHandler.END

def main() -> None:
    application = Application.builder().token(TELEGRAM_TOKEN).build()

    # --- Scheduler for Backups ---
    scheduler = AsyncIOScheduler()
    scheduler.add_job(backup_users, 'interval', hours=12, args=[application])
    scheduler.start()

    # --- Conversation Handlers ---
    creation_conv = ConversationHandler(
        entry_points=[CallbackQueryHandler(create_user_start, pattern='^create_user_start$')],
        states={
            GET_DESCRIPTION: [MessageHandler(filters.TEXT & ~filters.COMMAND, get_description)],
            GET_DURATION: [MessageHandler(filters.TEXT & ~filters.COMMAND, get_duration)],
        },
        fallbacks=[CallbackQueryHandler(start, pattern='^start$'), CommandHandler('cancel', cancel_conversation)],
    )
    restore_conv = ConversationHandler(
        entry_points=[CallbackQueryHandler(restore_backup_start, pattern='^restore_backup_start$')],
        states={
            GET_BACKUP_FILE: [MessageHandler(filters.Document.MimeType("application/json"), restore_from_file)]
        },
        fallbacks=[CallbackQueryHandler(start, pattern='^start$'), CommandHandler('cancel', cancel_conversation)],
    )

    # --- Register Handlers ---
    application.add_handler(CommandHandler("start", start))
    application.add_handler(CallbackQueryHandler(start, pattern='^start$'))
    application.add_handler(creation_conv)
    application.add_handler(restore_conv)
    application.add_handler(CallbackQueryHandler(manage_user_button_handler, pattern='^(extend|delete)_'))
    application.add_handler(MessageHandler(filters.TEXT & ~filters.COMMAND, handle_uuid_message))

    logger.info("Starting Telegram bot with scheduler...")
    application.run_polling()

if __name__ == '__main__':
    main()
