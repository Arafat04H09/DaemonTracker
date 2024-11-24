# Legion Command-Line Interface (CLI)

The **Legion CLI** is a daemon management tool designed to register, start, stop, monitor, and log processes. This application allows users to manage multiple background processes (daemons) efficiently.

## Features
- **Register Daemons**: Add a new daemon to the registry with a name and associated command.
- **Start Daemons**: Launch registered daemons and manage their states.
- **Stop Daemons**: Terminate active daemons gracefully.
- **Monitor Daemons**: Query the status of individual or all daemons, including their state and process IDs.
- **Log Management**: Rotate logs for daemons to maintain clean and manageable log files.
- **Signal Handling**: Responds to signals like `SIGINT`, `SIGALRM`, and `SIGCHLD` for proper process control and program termination.

## Build and Run
### Prerequisites
- A C compiler (e.g., `gcc`)
- Make sure you have the necessary permissions to manage processes and create log directories/files.

### Compilation
```bash
gcc -o legion_cli legion.c
