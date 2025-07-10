#!/bin/bash

# --- Configuration ---
# The name of the C source file
SOURCE_FILE="middle_server.c"
# The name of the compiled binary
EXECUTABLE="middle_server"
# The name for the systemd service
SERVICE_NAME="middle_server.service"
# The full path to the systemd service file
SERVICE_FILE="/etc/systemd/system/$SERVICE_NAME"
# The name of the log file
LOG_FILE="middle_server.log"
# Get the absolute path of the directory where the script is located
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"

# --- Helper Functions ---

# Function to check if the script is run as root
check_root() {
    if [ "$EUID" -ne 0 ]; then
        echo "This option requires root privileges. Please run with sudo."
        exit 1
    fi
}

# --- Main Functions ---

compile_and_start() {
    echo "--- 1. Compiling and Starting Server ---"
    check_root

    # A. Check for dependencies (gcc and hiredis development library)
    echo "Checking for dependencies (gcc, hiredis library)..."
    if ! command -v gcc &> /dev/null || ! dpkg -s libhiredis-dev &> /dev/null; then
        echo "Dependencies missing. Attempting to install..."
        apt-get update > /dev/null
        apt-get install -y gcc libhiredis-dev > /dev/null
        echo "Dependencies installed."
    else
        echo "Dependencies are already installed."
    fi

    # B. Compile the C code
    echo "Compiling $SOURCE_FILE..."
    if gcc "$SCRIPT_DIR/$SOURCE_FILE" -o "$SCRIPT_DIR/$EXECUTABLE" -lhiredis -lpthread; then
        echo "Compilation successful. Executable created: $EXECUTABLE"
    else
        echo "Compilation failed. Please check the C code for errors."
        exit 1
    fi

    # C. Create the log file and set permissions
    echo "Creating log file at $SCRIPT_DIR/$LOG_FILE..."
    touch "$SCRIPT_DIR/$LOG_FILE"
    # Allow the user running the service to write to the log file
    chown "$(logname):$(logname)" "$SCRIPT_DIR/$LOG_FILE"

    # D. Create the systemd service file
    echo "Creating systemd service file at $SERVICE_FILE..."
    cat > "$SERVICE_FILE" << EOF
[Unit]
Description=Middle Server for Storm_Route
After=network.target

[Service]
# The user that will run the service
User=$(logname)
# The group for the user
Group=$(logname)

# The directory where the executable is located
WorkingDirectory=$SCRIPT_DIR

# The command to start the server
ExecStart=$SCRIPT_DIR/$EXECUTABLE

# Restart the service if it crashes
Restart=always
RestartSec=5s

# Redirect stdout and stderr to our log file
StandardOutput=file:$SCRIPT_DIR/$LOG_FILE
StandardError=file:$SCRIPT_DIR/$LOG_FILE

[Install]
WantedBy=multi-user.target
EOF

    # E. Reload systemd, enable and start the service
    echo "Reloading systemd and starting the service..."
    systemctl daemon-reload
    systemctl enable "$SERVICE_NAME"
    systemctl start "$SERVICE_NAME"

    echo ""
    echo "--- Server is now running as a service! ---"
    echo "To see live logs, use option 4."
}

stop_service() {
    echo "--- 2. Stopping Server Service ---"
    check_root
    echo "Stopping $SERVICE_NAME..."
    systemctl stop "$SERVICE_NAME"
    echo "Service stopped."
}

delete_service() {
    echo "--- 3. Stopping and Deleting Service ---"
    check_root
    echo "Stopping service..."
    systemctl stop "$SERVICE_NAME"
    echo "Disabling service from starting on boot..."
    systemctl disable "$SERVICE_NAME"
    echo "Removing service file $SERVICE_FILE..."
    rm -f "$SERVICE_FILE"
    echo "Reloading systemd daemon..."
    systemctl daemon-reload
    echo "Service definition has been removed."
    echo "Note: The executable and log file have not been deleted."
}

show_logs() {
    echo "--- 4. Displaying Live Logs ---"
    echo "Showing logs from $LOG_FILE. Press Ctrl+C to exit."
    # Use tail -f to show live updates to the log file
    tail -f "$SCRIPT_DIR/$LOG_FILE"
}

# --- Menu Display ---
show_menu() {
    echo ""
    echo "================================="
    echo "  C Middle Server Manager"
    echo "================================="
    echo "1. Compile and Start Service"
    echo "2. Stop Service"
    echo "3. Stop and Delete Service"
    echo "4. Show Logs"
    echo "5. Exit"
    echo "---------------------------------"
}

# --- Main Loop ---
while true; do
    show_menu
    read -p "Please select an option [1-5]: " choice
    case $choice in
        1)
            compile_and_start
            ;;
        2)
            stop_service
            ;;
        3)
            delete_service
            ;;
        4)
            show_logs
            ;;
        5)
            echo "Exiting."
            exit 0
            ;;
        *)
            echo "Invalid option. Please try again."
            ;;
    esac
done
