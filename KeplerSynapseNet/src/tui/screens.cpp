#include "screens.h"

namespace synapse {
namespace tui {

const char* SYNAPSENET_LOGO[] = {
    "  ____                              _   _      _   ",
    " / ___| _   _ _ __   __ _ _ __  ___| \\ | | ___| |_ ",
    " \\___ \\| | | | '_ \\ / _` | '_ \\/ __|  \\| |/ _ \\ __|",
    "  ___) | |_| | | | | (_| | |_) \\__ \\ |\\  |  __/ |_ ",
    " |____/ \\__, |_| |_|\\__,_| .__/|___/_| \\_|\\___|\\__|",
    "        |___/            |_|                       ",
    "                                                   ",
    "      Decentralized AI Knowledge Network           ",
    "              Version 0.1.0-beta                   ",
    "                   by Kepler                       "
};
const int SYNAPSENET_LOGO_LINES = 10;

const char* KEPLER_LOGO[] = {
    "  _  __          _           ",
    " | |/ /___ _ __ | | ___ _ __ ",
    " | ' // _ \\ '_ \\| |/ _ \\ '__|",
    " | . \\  __/ |_) | |  __/ |   ",
    " |_|\\_\\___| .__/|_|\\___|_|   ",
    "          |_|                "
};
const int KEPLER_LOGO_LINES = 6;

const char* MAIN_MENU_ITEMS[] = {
    "1. Network Status",
    "2. Wallet",
    "3. Knowledge Base",
    "4. Model Inference",
    "5. Peer Discovery",
    "6. Consensus",
    "7. Settings",
    "8. Logs",
    "9. Exit"
};
const int MAIN_MENU_ITEMS_COUNT = 9;

const char* NETWORK_STATUS_FIELDS[] = {
    "Peers Connected",
    "Blocks Height",
    "Sync Progress",
    "Bandwidth In",
    "Bandwidth Out",
    "Uptime"
};
const int NETWORK_STATUS_FIELDS_COUNT = 6;

const char* WALLET_MENU_ITEMS[] = {
    "1. View Balance",
    "2. Send Transaction",
    "3. Receive Address",
    "4. Transaction History",
    "5. Export Keys",
    "6. Import Keys",
    "7. Back"
};
const int WALLET_MENU_ITEMS_COUNT = 7;

const char* SETTINGS_MENU_ITEMS[] = {
    "1. Network Settings",
    "2. Privacy Settings",
    "3. Model Settings",
    "4. Display Settings",
    "5. Security Settings",
    "6. Reset to Defaults",
    "7. Back"
};
const int SETTINGS_MENU_ITEMS_COUNT = 7;

const char* HELP_TEXT[] = {
    "SynapseNet Help",
    "===============",
    "",
    "Navigation:",
    "  Arrow keys - Navigate menus",
    "  Enter - Select option",
    "  Q - Quit current screen",
    "  H - Show this help",
    "",
    "Shortcuts:",
    "  Ctrl+N - Network status",
    "  Ctrl+W - Wallet",
    "  Ctrl+K - Knowledge base",
    "  Ctrl+M - Model inference",
    "  Ctrl+P - Peer discovery",
    "  Ctrl+S - Settings",
    "  Ctrl+L - Logs",
    "",
    "For more information, visit:",
    "https://synapsenet.io/docs"
};
const int HELP_TEXT_LINES = 20;

const char* STATUS_BAR_FORMAT = " SynapseNet v0.1 | Peers: %d | Height: %lu | %s ";

const char* LOADING_FRAMES[] = {
    "[    ]",
    "[=   ]",
    "[==  ]",
    "[=== ]",
    "[====]",
    "[ ===]",
    "[  ==]",
    "[   =]"
};
const int LOADING_FRAMES_COUNT = 8;

const char* CONFIRMATION_PROMPTS[] = {
    "Are you sure you want to proceed?",
    "This action cannot be undone.",
    "Press Y to confirm, N to cancel."
};
const int CONFIRMATION_PROMPTS_COUNT = 3;

const char* KNOWLEDGE_MENU_ITEMS[] = {
    "1. Browse Entries",
    "2. Search",
    "3. Add Entry",
    "4. Vote on Entry",
    "5. Categories",
    "6. Top Contributors",
    "7. My Entries",
    "8. Back"
};
const int KNOWLEDGE_MENU_ITEMS_COUNT = 8;

const char* MODEL_MENU_ITEMS[] = {
    "1. Load Model",
    "2. Unload Model",
    "3. Generate Text",
    "4. Chat Mode",
    "5. Model Info",
    "6. Inference Stats",
    "7. Configure",
    "8. Back"
};
const int MODEL_MENU_ITEMS_COUNT = 8;

const char* CONSENSUS_MENU_ITEMS[] = {
    "1. Validator Status",
    "2. Stake Tokens",
    "3. Unstake Tokens",
    "4. View Rewards",
    "5. Claim Rewards",
    "6. Validator List",
    "7. Epoch Info",
    "8. Back"
};
const int CONSENSUS_MENU_ITEMS_COUNT = 8;

const char* PEER_MENU_ITEMS[] = {
    "1. Connected Peers",
    "2. Add Peer",
    "3. Ban Peer",
    "4. Peer Stats",
    "5. DNS Seeds",
    "6. Export Peers",
    "7. Import Peers",
    "8. Back"
};
const int PEER_MENU_ITEMS_COUNT = 8;

const char* ERROR_MESSAGES[] = {
    "Connection failed. Check network settings.",
    "Invalid input. Please try again.",
    "Operation timed out.",
    "Insufficient balance.",
    "Permission denied.",
    "File not found.",
    "Invalid address format.",
    "Signature verification failed."
};
const int ERROR_MESSAGES_COUNT = 8;

const char* SUCCESS_MESSAGES[] = {
    "Operation completed successfully.",
    "Transaction sent.",
    "Settings saved.",
    "Connection established.",
    "Keys exported.",
    "Keys imported.",
    "Model loaded.",
    "Entry added."
};
const int SUCCESS_MESSAGES_COUNT = 8;

const char* SPINNER_FRAMES[] = {
    "|", "/", "-", "\\"
};
const int SPINNER_FRAMES_COUNT = 4;

const char* PROGRESS_BAR_CHARS[] = {
    " ", "▏", "▎", "▍", "▌", "▋", "▊", "▉", "█"
};
const int PROGRESS_BAR_CHARS_COUNT = 9;

const char* LOG_LEVEL_LABELS[] = {
    "TRACE",
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
    "FATAL"
};
const int LOG_LEVEL_LABELS_COUNT = 6;

const char* TRANSACTION_STATUS[] = {
    "Pending",
    "Confirming",
    "Confirmed",
    "Failed",
    "Cancelled"
};
const int TRANSACTION_STATUS_COUNT = 5;

const char* NODE_STATUS_LABELS[] = {
    "Starting",
    "Syncing",
    "Ready",
    "Stopping",
    "Stopped",
    "Error"
};
const int NODE_STATUS_LABELS_COUNT = 6;

const char* VALIDATOR_STATUS[] = {
    "Inactive",
    "Active",
    "Slashed",
    "Jailed",
    "Unbonding"
};
const int VALIDATOR_STATUS_COUNT = 5;

const char* NETWORK_HEALTH_LABELS[] = {
    "Excellent",
    "Good",
    "Fair",
    "Poor",
    "Critical"
};
const int NETWORK_HEALTH_LABELS_COUNT = 5;

const char* BOX_DRAWING_CHARS[] = {
    "┌", "┐", "└", "┘", "─", "│", "├", "┤", "┬", "┴", "┼"
};
const int BOX_DRAWING_CHARS_COUNT = 11;

const char* SYNC_STATUS_LABELS[] = {
    "Not Started", "Syncing Headers", "Syncing Blocks", "Verifying", "Complete"
};
const int SYNC_STATUS_LABELS_COUNT = 5;

const char* CONNECTION_STATUS_LABELS[] = {
    "Disconnected", "Connecting", "Connected", "Authenticated", "Error"
};
const int CONNECTION_STATUS_LABELS_COUNT = 5;

const char* CONSENSUS_STATUS_LABELS[] = {
    "Inactive", "Proposing", "Voting", "Committing", "Finalized"
};
const int CONSENSUS_STATUS_LABELS_COUNT = 5;

const char* MEMORY_STATUS_LABELS[] = {
    "Normal", "Warning", "Critical", "Out of Memory"
};
const int MEMORY_STATUS_LABELS_COUNT = 4;

const char* CPU_STATUS_LABELS[] = {
    "Idle", "Low", "Medium", "High", "Overload"
};
const int CPU_STATUS_LABELS_COUNT = 5;

}
}
