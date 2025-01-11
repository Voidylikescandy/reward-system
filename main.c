#define _XOPEN_SOURCE 700

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <stdarg.h>

struct currency {
    int currency_id;
    char currency_name[50];
    char symbol[10];
    int balance;
};

struct event {
    int event_id;
    char event_name[100];
    int currency_id;
    int is_time_limited;
    time_t start_time;
    time_t end_time;
    int is_active;
};

struct task {
    int event_id;
    int task_id;
    char task_description[256];
    int currency_amount;
    int is_completed;
};

struct store_item {
    int item_id;
    char item_description[256];
    int cost;
    int event_id;
    int stock;
    char category[50];
};

const char *sql_insert_currency = "INSERT INTO currency (currency_name, symbol, balance) VALUES (?, ?, ?);";
sqlite3_stmt *stmt_insert_currency;

const char *sql_insert_events = "INSERT INTO events (event_name, currency_id, is_time_limited, start_time, end_time, is_active) VALUES (?, ?, ?, ?, ?, ?);";
sqlite3_stmt *stmt_insert_events;

const char *sql_insert_tasks = "INSERT INTO tasks (event_id, task_id, task_description, currency_amount, is_completed) VALUES (?, ?, ?, ?, ?);";
sqlite3_stmt *stmt_insert_tasks;

const char *sql_insert_store = "INSERT INTO store (item_id, item_description, cost, event_id, stock, category) VALUES (?, ?, ?, ?, ?, ?);";
sqlite3_stmt *stmt_insert_store;

const char *sql_select_currency = "SELECT * FROM currency;";
sqlite3_stmt *stmt_select_currency;

const char *sql_select_active_events = "SELECT * FROM events WHERE is_active = 1;";
sqlite3_stmt *stmt_select_active_events;

const char *sql_select_incomplete_tasks_of_an_event = "SELECT * FROM tasks WHERE is_completed = 0 AND event_id = ?;";
sqlite3_stmt *stmt_select_incomplete_tasks_of_an_event;

const char *sql_update_task_completion = "UPDATE tasks SET is_completed = 1 WHERE task_id = ? AND event_id = ?;";
sqlite3_stmt *stmt_update_task_completion;

const char *sql_update_balance = "UPDATE currency SET balance = balance + ? WHERE currency_id = ?;";
sqlite3_stmt *stmt_update_balance;

const char *sql_select_store_items_of_an_event = "SELECT * FROM store WHERE event_id = ?;";
sqlite3_stmt *stmt_select_store_items_of_an_event;

const char *sql_update_store_stock = "UPDATE store SET stock = stock - 1 WHERE item_id = ? AND event_id = ? AND stock != -1;";
sqlite3_stmt *stmt_update_store_stock;

const char *sql_select_all_tasks_of_an_event = "SELECT * FROM tasks WHERE event_id = ?;";
sqlite3_stmt *stmt_select_all_tasks_of_an_event;

void handle_sigint(int sig, siginfo_t *info, void *context) {
    // Access the db pointer passed via the context
    sqlite3 *db = (sqlite3 *)info->si_value.sival_ptr;

    // Signal-safe message
    const char *msg = "\nCaught SIGINT (Ctrl+C). Finalizing statements, Closing database and exiting...\n";
    write(STDERR_FILENO, msg, strlen(msg));

    if (stmt_insert_currency) sqlite3_finalize(stmt_insert_currency);
    if (stmt_insert_events) sqlite3_finalize(stmt_insert_events);
    if (stmt_insert_tasks) sqlite3_finalize(stmt_insert_tasks);
    if (stmt_insert_store) sqlite3_finalize(stmt_insert_store);

    if (stmt_select_currency) sqlite3_finalize(stmt_select_currency);
    if (stmt_select_active_events) sqlite3_finalize(stmt_select_active_events);
    if (stmt_select_incomplete_tasks_of_an_event) sqlite3_finalize(stmt_select_incomplete_tasks_of_an_event);
    if (stmt_update_task_completion) sqlite3_finalize(stmt_update_task_completion);
    if (stmt_update_balance) sqlite3_finalize(stmt_update_balance);
    if (stmt_select_store_items_of_an_event) sqlite3_finalize(stmt_select_store_items_of_an_event);
    if (stmt_update_store_stock) sqlite3_finalize(stmt_update_store_stock);
    if (stmt_select_all_tasks_of_an_event) sqlite3_finalize(stmt_select_all_tasks_of_an_event);

    if (db) {
        int rc = sqlite3_close(db);
        if (rc != SQLITE_OK) {
            const char *err_msg = "Failed to close database.\n";
            write(STDERR_FILENO, err_msg, strlen(err_msg));
        } else {
            const char *success_msg = "Database connection closed.\n";
            write(STDERR_FILENO, success_msg, strlen(success_msg));
        }
    }

    exit(0);
}

void flush_input_buffer() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

// Function prototypes
int file_exists(const char *filename);
void create_tables(sqlite3 *db);
int prepare_statements(sqlite3 *db);
void display_menu();
void add_event(sqlite3 *db);
void mark_task_done(sqlite3 *db);
void buy_item(sqlite3 *db);
void list_events_and_tasks(sqlite3 *db);
void list_stats(sqlite3 *db);

int main() {
    sqlite3 *db;
    int rc;
    const char* db_file = "reward_system.db";

    if (!file_exists(db_file)) {
        printf("Configuration data does not exist...\n");

        rc = sqlite3_open(db_file, &db);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
            return 1;
        }

        create_tables(db);

    } else {

        rc = sqlite3_open(db_file, &db);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
            return 1;
        }
        
    }

    // Prepare all statements
    rc = prepare_statements(db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statements. Exiting...\n");
        sqlite3_close(db);
        return 1;
    }

    // Signal handler for Ctrl + C - SIGINT
    struct sigaction sa;
    sa.sa_sigaction = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;

    union sigval value;
    value.sival_ptr = db;
    sigaction(SIGINT, &sa, NULL);

    int choice;
    do {
        display_menu();
        scanf("%d", &choice);
        flush_input_buffer(); 

        switch (choice) {
            case 1:
                add_event(db);
                break;
            case 2:
                mark_task_done(db);
                break;
            case 3:
                buy_item(db);
                break;
            case 4:
                list_events_and_tasks(db);
                break;
            case 5:
                list_stats(db);
                break;
            case 6:
                printf("Exiting...\n");
                break;
            default:
                printf("Invalid choice. Please try again.\n");
        }
    } while (choice != 6);

    // Finalize all statements
    sqlite3_finalize(stmt_insert_currency);
    sqlite3_finalize(stmt_insert_events);
    sqlite3_finalize(stmt_insert_tasks);
    sqlite3_finalize(stmt_insert_store);

    sqlite3_finalize(stmt_select_currency);
    sqlite3_finalize(stmt_select_active_events);
    sqlite3_finalize(stmt_select_incomplete_tasks_of_an_event);
    sqlite3_finalize(stmt_update_task_completion);
    sqlite3_finalize(stmt_update_balance);
    sqlite3_finalize(stmt_select_store_items_of_an_event);
    sqlite3_finalize(stmt_update_store_stock);
    sqlite3_finalize(stmt_select_all_tasks_of_an_event);

    sqlite3_close(db);
    return 0;
}

int file_exists(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (file) {
        fclose(file);
        return 1;
    }
    return 0;
}

void create_tables(sqlite3 *db) {
    char *err_msg = 0;

    const char* sql_statements[] = {
        // Currency table
        "CREATE TABLE IF NOT EXISTS currency ("
        "currency_id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "currency_name TEXT NOT NULL,"
        "symbol TEXT NOT NULL,"
        "balance INTEGER NOT NULL DEFAULT 0"
        ");",

        // Events table
        "CREATE TABLE IF NOT EXISTS events ("
        "event_id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "event_name TEXT NOT NULL,"
        "currency_id INTEGER REFERENCES currency(currency_id),"
        "is_time_limited BOOLEAN NOT NULL,"
        "start_time TIMESTAMP,"
        "end_time TIMESTAMP,"
        "is_active BOOLEAN DEFAULT TRUE NOT NULL"
        ");",

        // Tasks table
        "CREATE TABLE IF NOT EXISTS tasks ("
        "event_id INTEGER REFERENCES events(event_id),"
        "task_id INTEGER NOT NULL,"
        "task_description TEXT NOT NULL,"
        "currency_amount INTEGER NOT NULL,"
        "is_completed BOOLEAN DEFAULT FALSE NOT NULL,"
        "PRIMARY KEY (event_id, task_id)"
        ");",

        // Store table
        "CREATE TABLE IF NOT EXISTS store ("
        "item_id INTEGER NOT NULL,"
        "item_description TEXT NOT NULL,"
        "cost INTEGER NOT NULL DEFAULT 0,"
        "event_id INTEGER REFERENCES events(event_id),"
        "stock INTEGER NOT NULL DEFAULT -1,"
        "category TEXT,"
        "PRIMARY KEY (event_id, item_id)"
        ");"
    };

    // Enable foreign key constraints
    int rc = sqlite3_exec(db, "PRAGMA foreign_keys = ON;", 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to enable foreign keys: %s\n", err_msg);
        sqlite3_free(err_msg);
        return;
    }

    for (int i = 0; i < sizeof(sql_statements) / sizeof(sql_statements[0]); i++) {
        rc = sqlite3_exec(db, sql_statements[i], 0, 0, &err_msg);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "Failed to create table: %s\n", err_msg);
            sqlite3_free(err_msg);
            return;
        }
    }

    printf("Tables created successfully.\n");
}

int prepare_statements(sqlite3 *db) {
    int rc;

    rc = sqlite3_prepare_v2(db, sql_insert_currency, -1, &stmt_insert_currency, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        return rc;
    }

    rc = sqlite3_prepare_v2(db, sql_insert_events, -1, &stmt_insert_events, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt_insert_currency);
        return rc;
    }

    rc = sqlite3_prepare_v2(db, sql_insert_tasks, -1, &stmt_insert_tasks, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt_insert_currency);
        sqlite3_finalize(stmt_insert_events);
        return rc;
    }

    rc = sqlite3_prepare_v2(db, sql_insert_store, -1, &stmt_insert_store, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt_insert_currency);
        sqlite3_finalize(stmt_insert_events);
        sqlite3_finalize(stmt_insert_tasks);
        return rc;
    }

    rc = sqlite3_prepare_v2(db, sql_select_currency, -1, &stmt_select_currency, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt_insert_currency);
        sqlite3_finalize(stmt_insert_events);
        sqlite3_finalize(stmt_insert_tasks);
        sqlite3_finalize(stmt_insert_store);
        return rc;
    }

    rc = sqlite3_prepare_v2(db, sql_select_active_events, -1, &stmt_select_active_events, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt_insert_currency);
        sqlite3_finalize(stmt_insert_events);
        sqlite3_finalize(stmt_insert_tasks);
        sqlite3_finalize(stmt_insert_store);
        sqlite3_finalize(stmt_select_currency);
        return rc;
    }

    rc = sqlite3_prepare_v2(db, sql_select_incomplete_tasks_of_an_event, -1, &stmt_select_incomplete_tasks_of_an_event, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt_insert_currency);
        sqlite3_finalize(stmt_insert_events);
        sqlite3_finalize(stmt_insert_tasks);
        sqlite3_finalize(stmt_insert_store);
        sqlite3_finalize(stmt_select_currency);
        sqlite3_finalize(stmt_select_active_events);
        return rc;
    }

    rc = sqlite3_prepare_v2(db, sql_update_task_completion, -1, &stmt_update_task_completion, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt_insert_currency);
        sqlite3_finalize(stmt_insert_events);
        sqlite3_finalize(stmt_insert_tasks);
        sqlite3_finalize(stmt_insert_store);
        sqlite3_finalize(stmt_select_currency);
        sqlite3_finalize(stmt_select_active_events);
        sqlite3_finalize(stmt_select_incomplete_tasks_of_an_event);
        return rc;
    }

    rc = sqlite3_prepare_v2(db, sql_update_balance, -1, &stmt_update_balance, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt_insert_currency);
        sqlite3_finalize(stmt_insert_events);
        sqlite3_finalize(stmt_insert_tasks);
        sqlite3_finalize(stmt_insert_store);
        sqlite3_finalize(stmt_select_currency);
        sqlite3_finalize(stmt_select_active_events);
        sqlite3_finalize(stmt_select_incomplete_tasks_of_an_event);
        sqlite3_finalize(stmt_update_task_completion);
        return rc;
    }

    rc = sqlite3_prepare_v2(db, sql_select_store_items_of_an_event, -1, &stmt_select_store_items_of_an_event, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt_insert_currency);
        sqlite3_finalize(stmt_insert_events);
        sqlite3_finalize(stmt_insert_tasks);
        sqlite3_finalize(stmt_insert_store);
        sqlite3_finalize(stmt_select_currency);
        sqlite3_finalize(stmt_select_active_events);
        sqlite3_finalize(stmt_select_incomplete_tasks_of_an_event);
        sqlite3_finalize(stmt_update_task_completion);
        sqlite3_finalize(stmt_update_balance);
        return rc;
    }

    rc = sqlite3_prepare_v2(db, sql_update_store_stock, -1, &stmt_update_store_stock, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt_insert_currency);
        sqlite3_finalize(stmt_insert_events);
        sqlite3_finalize(stmt_insert_tasks);
        sqlite3_finalize(stmt_insert_store);
        sqlite3_finalize(stmt_select_currency);
        sqlite3_finalize(stmt_select_active_events);
        sqlite3_finalize(stmt_select_incomplete_tasks_of_an_event);
        sqlite3_finalize(stmt_update_task_completion);
        sqlite3_finalize(stmt_update_balance);
        sqlite3_finalize(stmt_select_store_items_of_an_event);
        return rc;
    }

    rc = sqlite3_prepare_v2(db, sql_select_all_tasks_of_an_event, -1, &stmt_select_all_tasks_of_an_event, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt_insert_currency);
        sqlite3_finalize(stmt_insert_events);
        sqlite3_finalize(stmt_insert_tasks);
        sqlite3_finalize(stmt_insert_store);
        sqlite3_finalize(stmt_select_currency);
        sqlite3_finalize(stmt_select_active_events);
        sqlite3_finalize(stmt_select_incomplete_tasks_of_an_event);
        sqlite3_finalize(stmt_update_task_completion);
        sqlite3_finalize(stmt_update_balance);
        sqlite3_finalize(stmt_select_store_items_of_an_event);
        sqlite3_finalize(stmt_update_store_stock);
        return rc;
    }


    printf("All statements prepared successfully.\n");
    return SQLITE_OK;
}

void display_menu() {
    printf("\n--- Reward System Menu ---\n");
    printf("1. Add an Event\n");
    printf("2. Mark a Task as Done\n");
    printf("3. Buy an Item from the Store\n");
    printf("4. List All Events and Their Tasks\n");
    printf("5. List My Stats\n");
    printf("6. Exit\n");
    printf("Enter your choice: ");
}

void print_top_border(int num_columns, ...) {
    va_list args;
    va_start(args, num_columns);

    printf("┌─");
    for (int i = 0; i < num_columns; ++i) {
        int column_width = va_arg(args, int);
        for (int j = 0; j < column_width; ++j) printf("─");
        if (i < num_columns - 1) printf("─┬─");
    }
    printf("─┐\n");

    va_end(args);
}

void print_row_separator(int num_columns, ...) {
    va_list args;
    va_start(args, num_columns);

    printf("├─");
    for (int i = 0; i < num_columns; ++i) {
        int column_width = va_arg(args, int);
        for (int j = 0; j < column_width; ++j) printf("─");
        if (i < num_columns - 1) printf("─┼─");
    }
    printf("─┤\n");

    va_end(args);
}

void print_bottom_border(int num_columns, ...) {
    va_list args;
    va_start(args, num_columns);

    printf("└─");
    for (int i = 0; i < num_columns; ++i) {
        int column_width = va_arg(args, int);
        for (int j = 0; j < column_width; ++j) printf("─");
        if (i < num_columns - 1) printf("─┴─");
    }
    printf("─┘\n");

    va_end(args);
}

void print_table_row(int num_columns, ...) {
    va_list args;
    va_start(args, num_columns);

    printf("│");

    for (int i = 0; i < num_columns; i++) {
        const char *column_value = va_arg(args, const char *);  
        int column_width = va_arg(args, int);                  

        printf(" %-*s │", column_width, column_value);
    }
    printf("\n");

    va_end(args);
}

int create_new_currency(sqlite3 *db) {
    struct currency new_currency;

    printf("Enter currency name: ");
    fgets(new_currency.currency_name, sizeof(new_currency.currency_name), stdin);
    new_currency.currency_name[strcspn(new_currency.currency_name, "\n")] = 0;

    printf("Enter currency symbol: ");
    fgets(new_currency.symbol, sizeof(new_currency.symbol), stdin);
    new_currency.symbol[strcspn(new_currency.symbol, "\n")] = 0;

    new_currency.balance = 0;

    sqlite3_bind_text(stmt_insert_currency, 1, new_currency.currency_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt_insert_currency, 2, new_currency.symbol, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt_insert_currency, 3, new_currency.balance);

    int rc = sqlite3_step(stmt_insert_currency);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failed to insert currency: %s\n", sqlite3_errmsg(db));
        sqlite3_reset(stmt_insert_currency);
        return -1;
    }

    sqlite3_reset(stmt_insert_currency);

    return sqlite3_last_insert_rowid(db);
}

void add_event(sqlite3 *db) {
    struct event new_event;
    int rc;

    // Event name
    printf("Enter event name: ");
    fgets(new_event.event_name, sizeof(new_event.event_name), stdin);
    new_event.event_name[strcspn(new_event.event_name, "\n")] = 0;

    printf("Existing currencies\n");
    int id_width = 10;
    int name_width = 20;
    int symbol_width = 10;
    print_top_border(3, id_width, name_width, symbol_width);
    print_table_row(3, "ID", id_width, "Name", name_width, "Symbol", symbol_width);
    print_row_separator(3, id_width, name_width, symbol_width);
    
    // Currency ID
    int currency_count = 0;
    while ((rc = sqlite3_step(stmt_select_currency)) == SQLITE_ROW) {
        int currency_id = sqlite3_column_int(stmt_select_currency, 0);
        const char *currency_name = (const char *)sqlite3_column_text(stmt_select_currency, 1);
        const char *symbol = (const char *)sqlite3_column_text(stmt_select_currency, 2);
        int balance = sqlite3_column_int(stmt_select_currency, 3);

        char id_str[10];
        snprintf(id_str, sizeof(id_str), "%d", currency_id);

        print_table_row(3, id_str, id_width, currency_name, name_width, symbol, symbol_width);
        currency_count++;
    }

    print_bottom_border(3, id_width, name_width, symbol_width);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failed to fetch currencies: %s\n", sqlite3_errmsg(db));
        return;
    }
    
    sqlite3_reset(stmt_select_currency);

    if (currency_count == 0) {
        printf("There are no currencies available, make one.\n");
        new_event.currency_id = create_new_currency(db);
        if (new_event.currency_id == -1) {
            fprintf(stderr, "Failed to create new currency.\n");
            return;
        }
        printf("New currency created with ID: %d\n", new_event.currency_id);
    } else {
        printf("\nChoose an existing currency or create a new one(0): ");
        scanf("%d", &new_event.currency_id);
        flush_input_buffer();

        if (new_event.currency_id == 0) {
            new_event.currency_id = create_new_currency(db);
            if (new_event.currency_id == -1) {
                fprintf(stderr, "Failed to create new currency.\n");
                return;
            }
            printf("New currency created with ID: %d\n", new_event.currency_id);
        }
    }

    // Is time limited
    printf("Is this event time-limited? (1 for Yes, 0 for No): ");
    scanf("%d", &new_event.is_time_limited);
    flush_input_buffer();

    if (new_event.is_time_limited) {
        printf("Enter start time (YYYY-MM-DD HH:MM:SS): ");
        char start_time_str[21];
        fgets(start_time_str, sizeof(start_time_str), stdin);
        start_time_str[strcspn(start_time_str, "\n")] = 0;


        printf("Enter end time (YYYY-MM-DD HH:MM:SS): ");
        char end_time_str[21];
        fgets(end_time_str, sizeof(end_time_str), stdin);
        end_time_str[strcspn(end_time_str, "\n")] = 0;

        struct tm tm = {0};
        
        strptime(start_time_str, "%Y-%m-%d %H:%M:%S", &tm);
        new_event.start_time = mktime(&tm);

        strptime(end_time_str, "%Y-%m-%d %H:%M:%S", &tm);
        new_event.end_time = mktime(&tm);
    } else {
        new_event.start_time = 0;
        new_event.end_time = 0;
    }

    sqlite3_bind_text(stmt_insert_events, 1, new_event.event_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt_insert_events, 2, new_event.currency_id);
    sqlite3_bind_int(stmt_insert_events, 3, new_event.is_time_limited);
    if (new_event.is_time_limited) {
        sqlite3_bind_int64(stmt_insert_events, 4, new_event.start_time);
        sqlite3_bind_int64(stmt_insert_events, 5, new_event.end_time);
    } else {
        sqlite3_bind_null(stmt_insert_events, 4);
        sqlite3_bind_null(stmt_insert_events, 5);
    }
    sqlite3_bind_int(stmt_insert_events, 6, 1);

    rc = sqlite3_step(stmt_insert_events);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Error adding event: %s\n", sqlite3_errmsg(db));
        sqlite3_reset(stmt_insert_events);
        return;
    }

    sqlite3_reset(stmt_insert_events);
    printf("Event added successfully\n");
    new_event.event_id = sqlite3_last_insert_rowid(db);

    printf("Enter the number of tasks for Event %s: ", new_event.event_name);
    int num_tasks;
    scanf("%d", &num_tasks);
    flush_input_buffer();

    for (int i = 1; i <= num_tasks; ++i) {
        struct task new_task;

        new_task.event_id = new_event.event_id;
        new_task.task_id = i;

        printf("Enter task description: ");
        fgets(new_task.task_description, sizeof(new_task.task_description), stdin);
        new_task.task_description[strcspn(new_task.task_description, "\n")] = 0;

        printf("Enter the currency amount rewarded upon completion: ");
        scanf("%d", &new_task.currency_amount);
        flush_input_buffer();

        new_task.is_completed = 0;

        sqlite3_bind_int(stmt_insert_tasks, 1, new_task.event_id);
        sqlite3_bind_int(stmt_insert_tasks, 2, new_task.task_id);
        sqlite3_bind_text(stmt_insert_tasks, 3, new_task.task_description, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt_insert_tasks, 4, new_task.currency_amount);
        sqlite3_bind_int(stmt_insert_tasks, 5, new_task.is_completed);

        rc = sqlite3_step(stmt_insert_tasks);
        if (rc != SQLITE_DONE) {
            fprintf(stderr, "Error adding task: %s\n", sqlite3_errmsg(db));
            sqlite3_reset(stmt_insert_tasks);
            return;
        }

        sqlite3_reset(stmt_insert_tasks);
        printf("Task %d added successfully\n", i);
    }

    printf("Enter the number of store items associated with this event: ");
    int num_items;
    scanf("%d", &num_items);
    flush_input_buffer();

    for (int i = 1; i <= num_items; ++i) {
        struct store_item new_store_item;

        new_store_item.item_id = i;

        printf("Enter item description: ");
        fgets(new_store_item.item_description, sizeof(new_store_item.item_description), stdin);
        new_store_item.item_description[strcspn(new_store_item.item_description, "\n")] = 0;

        printf("Enter cost of the item: ");
        scanf("%d", &new_store_item.cost);
        flush_input_buffer();

        new_store_item.event_id = new_event.event_id;

        printf("Enter item stock(-1 for infinity): ");
        scanf("%d", &new_store_item.stock);
        flush_input_buffer();

        printf("Enter category: ");
        fgets(new_store_item.category, sizeof(new_store_item.category), stdin);
        new_store_item.category[strcspn(new_store_item.category, "\n")] = 0;

        sqlite3_bind_int(stmt_insert_store, 1, new_store_item.item_id);
        sqlite3_bind_text(stmt_insert_store, 2, new_store_item.item_description, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt_insert_store, 3, new_store_item.cost);
        sqlite3_bind_int(stmt_insert_store, 4, new_store_item.event_id);
        sqlite3_bind_int(stmt_insert_store, 5, new_store_item.stock);
        sqlite3_bind_text(stmt_insert_store, 6, new_store_item.category, -1, SQLITE_TRANSIENT);

        rc = sqlite3_step(stmt_insert_store);
        if (rc != SQLITE_DONE) {
            fprintf(stderr, "Error adding item: %s\n", sqlite3_errmsg(db));
            sqlite3_reset(stmt_insert_store);
            return;
        }

        sqlite3_reset(stmt_insert_store);
        printf("Item %d added successfully\n", i);
    }
}

void print_events_table(struct event *events, int event_count) {
    int id_width = 10;
    int name_width = 30;
    int time_width = 30;

    print_top_border(4, id_width, name_width, time_width, time_width);
    print_table_row(4, "ID", id_width, "Name", name_width, "Start Time", time_width, "End Time", time_width);
    print_row_separator(4, id_width, name_width, time_width, time_width);

    for (int i = 0; i < event_count; ++i) {
        char start_time_str[21] = "N/A";
        char end_time_str[21] = "N/A";

        if (events[i].start_time != -1 && events[i].end_time != -1) {
            struct tm *start_tm = localtime(&events[i].start_time);
            struct tm *end_tm = localtime(&events[i].end_time);

            strftime(start_time_str, sizeof(start_time_str), "%Y-%m-%d %H:%M:%S", start_tm);
            strftime(end_time_str, sizeof(end_time_str), "%Y-%m-%d %H:%M:%S", end_tm);
        }

        char id_str[10];
        snprintf(id_str, sizeof(id_str), "%d", events[i].event_id);

        print_table_row(4, id_str, id_width, events[i].event_name, name_width, start_time_str, time_width, end_time_str, time_width);

        // if (i < event_count - 1) {
        //     print_row_separator(4, id_width, name_width, time_width, time_width);
        // }
    }

    print_bottom_border(4, id_width, name_width, time_width, time_width);
}

struct event * get_active_events(sqlite3 *db, int *event_count) {
    int rc;
    struct event *events = NULL;
    *event_count = 0;
    int event_capacity = 10;
    events = malloc(event_capacity * sizeof(struct event));
    if (!events) {
        fprintf(stderr, "Unable to allocate memory for events.\n");
        return NULL;
    }

    while ((rc = sqlite3_step(stmt_select_active_events)) == SQLITE_ROW) {
        if (*event_count >= event_capacity) {
            event_capacity *= 2;
            struct event *new_events = realloc(events, event_capacity * sizeof(struct event));
            if (!new_events) {
                fprintf(stderr, "Unable to reallocate events.\n");
                free(events);
                return NULL;
            }
            events = new_events;
        }

        events[*event_count].event_id = sqlite3_column_int(stmt_select_active_events, 0);
        const char *event_name = (const char *)sqlite3_column_text(stmt_select_active_events, 1);
        strncpy(events[*event_count].event_name, event_name, sizeof(events[*event_count].event_name) - 1);
        events[*event_count].event_name[sizeof(events[*event_count].event_name) - 1] = '\0';
        events[*event_count].currency_id = sqlite3_column_int(stmt_select_active_events, 2);
        events[*event_count].is_time_limited = sqlite3_column_int(stmt_select_active_events, 3);

        if (sqlite3_column_type(stmt_select_active_events, 4) == SQLITE_NULL) {
            events[*event_count].start_time = -1;
        } else {
            events[*event_count].start_time = sqlite3_column_int64(stmt_select_active_events, 4);
        }

        if (sqlite3_column_type(stmt_select_active_events, 5) == SQLITE_NULL) {
            events[*event_count].end_time = -1;
        } else {
            events[*event_count].end_time = sqlite3_column_int64(stmt_select_active_events, 5);
        }
        
        events[*event_count].is_active = sqlite3_column_int(stmt_select_active_events, 6);

        (*event_count)++;
    }

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Error fetching active rows: %s\n", sqlite3_errmsg(db));
        free(events);
        sqlite3_reset(stmt_select_active_events);
        return NULL;
    }

    sqlite3_reset(stmt_select_active_events);
    return events;
}

struct task * get_incomplete_tasks_of_an_event(sqlite3 *db, int *task_count, int event_id) {
    int rc;
    struct task *tasks = NULL;
    *task_count = 0;
    int task_capacity = 10;
    tasks = malloc(task_capacity * sizeof(struct task));
    if (!tasks) {
        fprintf(stderr, "Unable to allocate memory for tasks.\n");
        return NULL;
    }

    sqlite3_bind_int(stmt_select_incomplete_tasks_of_an_event, 1, event_id);

    while ((rc = sqlite3_step(stmt_select_incomplete_tasks_of_an_event)) == SQLITE_ROW) {
        if (*task_count >= task_capacity) {
            task_capacity *= 2;
            struct task *new_tasks = realloc(tasks, task_capacity * sizeof(struct task));
            if (!new_tasks) {
                fprintf(stderr, "Unable to reallocate tasks.\n");
                sqlite3_reset(stmt_select_incomplete_tasks_of_an_event);
                free(tasks);
                return NULL;
            }
            tasks = new_tasks;
        }

        tasks[*task_count].event_id = sqlite3_column_int(stmt_select_incomplete_tasks_of_an_event, 0);
        tasks[*task_count].task_id = sqlite3_column_int(stmt_select_incomplete_tasks_of_an_event, 1);
        const char *task_name = (const char *)sqlite3_column_text(stmt_select_incomplete_tasks_of_an_event, 2);
        strncpy(tasks[*task_count].task_description, task_name, sizeof(tasks[*task_count].task_description) - 1);
        tasks[*task_count].task_description[sizeof(tasks[*task_count].task_description) - 1] = '\0';
        tasks[*task_count].currency_amount = sqlite3_column_int(stmt_select_incomplete_tasks_of_an_event, 3);
        tasks[*task_count].is_completed = sqlite3_column_int(stmt_select_incomplete_tasks_of_an_event, 4);

        (*task_count)++;
    }

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Error fetching tasks: %s\n", sqlite3_errmsg(db));
        free(tasks);
        sqlite3_reset(stmt_select_incomplete_tasks_of_an_event);
        return NULL;
    }

    sqlite3_reset(stmt_select_incomplete_tasks_of_an_event);
    return tasks;
}

void print_tasks_table(struct task *tasks, int task_count) {
    int id_width = 10;
    int desc_width = 100;
    int amount_width = 20;

    print_top_border(3, id_width, desc_width, amount_width);
    print_table_row(3, "ID", id_width, "Task Description", desc_width, "Amount", amount_width);
    print_row_separator(3, id_width, desc_width, amount_width);

    for (int i = 0; i < task_count; ++i) {
        char id_str[10];
        snprintf(id_str, sizeof(id_str), "%d", tasks[i].task_id);

        char amount_str[10];
        snprintf(amount_str, sizeof(amount_str), "%d", tasks[i].currency_amount);

        print_table_row(3, id_str, id_width, tasks[i].task_description, desc_width, amount_str, amount_width);
    }

    print_bottom_border(3, id_width, desc_width, amount_width);
}

struct currency * get_currencies(sqlite3 *db, int *currency_count) {
    int rc;
    struct currency *currencies;
    *currency_count = 0;
    int currency_capacity = 10;
    currencies = malloc(currency_capacity * sizeof(struct currency));
    if (!currencies) {
        fprintf(stderr, "Failed to allocated memory for currencies.\n");
        return NULL;
    }

    while ((rc = sqlite3_step(stmt_select_currency)) == SQLITE_ROW) {
        if (*currency_count >= currency_capacity) {
            currency_capacity *= 2;
            struct currency *new_currencies = realloc(currencies, currency_capacity * sizeof(struct currency));
            if (!new_currencies) {
                fprintf(stderr, "Failed to reallocate currencies.\n");
                sqlite3_reset(stmt_select_currency);
                free(currencies);
                return NULL;
            }
            currencies = new_currencies;
        }

        currencies[*currency_count].currency_id = sqlite3_column_int(stmt_select_currency, 0);
        const char *currency_name = (const char *)sqlite3_column_text(stmt_select_currency, 1);
        strncpy(currencies[*currency_count].currency_name, currency_name, sizeof(currencies[*currency_count].currency_name) - 1);
        currencies[*currency_count].currency_name[sizeof(currencies[*currency_count]) - 1] = '\0';
        const char *symbol = (const char *)sqlite3_column_text(stmt_select_currency, 2);
        strncpy(currencies[*currency_count].symbol, symbol, sizeof(currencies[*currency_count].symbol) - 1);
        currencies[*currency_count].symbol[sizeof(currencies[*currency_count].symbol) - 1] = '\0';
        currencies[*currency_count].balance = sqlite3_column_int(stmt_select_currency, 3);

        (*currency_count)++;
    }

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Error fetching currencies: %s\n", sqlite3_errmsg(db));
        free(currencies);
        sqlite3_reset(stmt_select_incomplete_tasks_of_an_event);
        return NULL;
    }

    sqlite3_reset(stmt_select_currency);
    return currencies;
}

void print_currency_table(struct currency *currencies, int currency_count) {
    int id_width = 10;
    int name_width = 20;
    int symbol_width = 10;
    int balance_width = 20;

    print_top_border(4, id_width, name_width, symbol_width, balance_width);
    print_table_row(4, "ID", id_width, "Name", name_width, "Symbol", symbol_width, "Balance", balance_width);
    print_row_separator(4, id_width, name_width, symbol_width, balance_width);

    for (int i = 0; i < currency_count; ++i) {
        char id_str[10];
        snprintf(id_str, sizeof(id_str), "%d", currencies[i].currency_id);

        char bal_str[10];
        snprintf(bal_str, sizeof(bal_str), "%d", currencies[i].balance);

        print_table_row(4, id_str, id_width, currencies[i].currency_name, name_width, currencies[i].symbol, symbol_width, bal_str, balance_width);
    }

    print_bottom_border(4, id_width, name_width, symbol_width, balance_width);
}

void mark_task_done(sqlite3 *db) {
    int rc;

    int event_count;
    struct event *events = get_active_events(db, &event_count);

    print_events_table(events, event_count);

    int chosen_event_id;
    printf("Choose which event the task belongs to: ");
    scanf("%d", &chosen_event_id);
    flush_input_buffer();

    int chosen_currency_id = -1;
    for (int i = 0; i < event_count; ++i) {
        if (events[i].event_id == chosen_event_id) {
            chosen_currency_id = events[i].currency_id;
            break;
        }
    }
    free(events);
    if (chosen_currency_id == -1) {
        fprintf(stderr, "Could not find currency ID.\n");
        return;
    }

    int task_count;
    struct task *tasks = get_incomplete_tasks_of_an_event(db, &task_count, chosen_event_id);

    if (task_count == 0) {
        printf("No tasks left.\n");
        return;
    }

    print_tasks_table(tasks, task_count);

    int chosen_task_id;
    printf("Choose completed task: ");
    scanf("%d", &chosen_task_id);
    flush_input_buffer();

    int currency_amount = -1;
    for (int i = 0; i < task_count; ++i) {
        if (tasks[i].task_id == chosen_task_id) {
            currency_amount = tasks[i].currency_amount;
            break;
        }
    }
    free(tasks);
    if (currency_amount == -1) {
        fprintf(stderr, "Could not find currency amount.\n");
        return;
    }
    
    sqlite3_bind_int(stmt_update_task_completion, 1, chosen_task_id);
    sqlite3_bind_int(stmt_update_task_completion, 2, chosen_event_id);

    rc = sqlite3_step(stmt_update_task_completion);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failure in updating completion: %s\n", sqlite3_errmsg(db));
        sqlite3_reset(stmt_update_task_completion);
        return;
    }

    sqlite3_reset(stmt_update_task_completion);
    printf("Task %d successfully completed. Keep it up!\n", chosen_task_id);

    sqlite3_bind_int(stmt_update_balance, 1, currency_amount);
    sqlite3_bind_int(stmt_update_balance, 2, chosen_currency_id);

    rc = sqlite3_step(stmt_update_balance);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Error updating balance: %s\n", sqlite3_errmsg(db));
        sqlite3_reset(stmt_update_balance);
        return;
    }

    sqlite3_reset(stmt_update_balance);

    int currency_count;
    struct currency *currencies = get_currencies(db, &currency_count);

    int idx_in_array = -1;
    for (int i = 0; i < currency_count; ++i) {
        if (chosen_currency_id == currencies[i].currency_id) {
            idx_in_array = i;
            break;
        }
    }
    if (idx_in_array == -1) {
        fprintf(stderr, "Could not find currency index.\n");
        free(currencies);
        return;
    }

    printf("Currency %d has increased by %d %ss. Happy spending!\n", chosen_currency_id, currency_amount, currencies[idx_in_array].symbol);

    printf("Current Balance\n");
    print_currency_table(currencies, currency_count);
    free(currencies);
}

struct store_item * get_store_items_by_event(sqlite3 *db, int *store_item_count, int chosen_event_id) {
    int rc;
    struct store_item *store_items;
    *store_item_count = 0;
    int store_item_capacity = 10;
    store_items = malloc(store_item_capacity * sizeof(struct store_item));
    if (!store_items) {
        fprintf(stderr, "Unable to allocate memory for store items.\n");
        return NULL;
    }

    sqlite3_bind_int(stmt_select_store_items_of_an_event, 1, chosen_event_id);

    while ((rc = sqlite3_step(stmt_select_store_items_of_an_event)) == SQLITE_ROW) {
        if (*store_item_count >= store_item_capacity) {
            store_item_capacity *= 2;
            struct store_item *new_store_items = realloc(store_items, store_item_capacity * sizeof(struct store_item));
            if (!new_store_items) {
                fprintf(stderr, "Failed to reallocate store items.\n");
                sqlite3_reset(stmt_select_store_items_of_an_event);
                free(store_items);
                return NULL;
            }
            store_items = new_store_items;
        }

        store_items[*store_item_count].item_id = sqlite3_column_int(stmt_select_store_items_of_an_event, 0);
        const char *item_description = (const char *)sqlite3_column_text(stmt_select_store_items_of_an_event, 1);
        strncpy(store_items[*store_item_count].item_description, item_description, sizeof(store_items[*store_item_count].item_description) - 1);
        store_items[*store_item_count].item_description[sizeof(store_items[*store_item_count].item_description) - 1] = '\0';
        store_items[*store_item_count].cost = sqlite3_column_int(stmt_select_store_items_of_an_event, 2);
        store_items[*store_item_count].event_id = sqlite3_column_int(stmt_select_store_items_of_an_event, 3);
        store_items[*store_item_count].stock = sqlite3_column_int(stmt_select_store_items_of_an_event, 4);
        const char *category = (const char *)sqlite3_column_text(stmt_select_store_items_of_an_event, 5);
        strncpy(store_items[*store_item_count].category, category, sizeof(store_items[*store_item_count].category) - 1);
        store_items[*store_item_count].category[sizeof(store_items[*store_item_count].category) - 1];

        (*store_item_count)++;

    }

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Error in fetching store items: %s\n", sqlite3_errmsg(db));
        free(store_items);
        sqlite3_reset(stmt_select_store_items_of_an_event);
        return NULL;
    }

    sqlite3_reset(stmt_select_store_items_of_an_event);
    return store_items;
}

void buy_item(sqlite3 *db) {
    int rc;

    int currency_count;
    struct currency *currencies = get_currencies(db, &currency_count);

    int event_count;
    struct event *events = get_active_events(db, &event_count);

    int e_id_width = 10;
    int e_name_width = 20;
    int time_width = 30;
    int c_name_width = 30;
    int bal_width = 20;

    print_top_border(6, e_id_width, e_name_width, time_width, time_width, c_name_width, bal_width);
    print_table_row(6, "ID", e_id_width, "Event Name", e_name_width, "Start Time", time_width, "End Time", time_width, "Currency", c_name_width, "Balance", bal_width);
    print_row_separator(6, e_id_width, e_name_width, time_width, time_width, c_name_width, bal_width);

    for (int i = 0; i < event_count; ++i) {
        char id_str[10];
        snprintf(id_str, sizeof(id_str), "%d", events[i].event_id);

        char start_time_str[21] = "N/A";
        char end_time_str[21] = "N/A";

        if (events[i].start_time != -1 && events[i].end_time != -1) {
            struct tm *start_tm = localtime(&events[i].start_time);
            struct tm *end_tm = localtime(&events[i].end_time);

            strftime(start_time_str, sizeof(start_time_str), "%Y-%m-%d %H:%M:%S", start_tm);
            strftime(end_time_str, sizeof(end_time_str), "%Y-%m-%d %H:%M:%S", end_tm);
        }

        for (int j = 0; j < currency_count; ++j) {
            if (currencies[j].currency_id == events[i].currency_id) {
                char bal_str[20];
                snprintf(bal_str, sizeof(bal_str), "%d %ss", currencies[j].balance, currencies[j].symbol);

                print_table_row(6, id_str, e_id_width, events[i].event_name, e_name_width, start_time_str, time_width, end_time_str, time_width, currencies[j].currency_name, c_name_width, bal_str, bal_width);
                break;
            }
        }
    }
    print_bottom_border(6, e_id_width, e_name_width, time_width, time_width, c_name_width, bal_width);
    
    printf("Enter event associated with the store: ");
    int chosen_event_id;
    scanf("%d", &chosen_event_id);
    flush_input_buffer();

    int chosen_currency_id = -1;
    for (int i = 0; i < event_count; ++i) {
        if (events[i].event_id == chosen_event_id) {
            chosen_currency_id = events[i].currency_id;
            break;
        }
    }
    if (chosen_currency_id == -1) {
        fprintf(stderr, "Could not find currency ID.\n");
        free(currencies);
        free(events);
        return;
    }

    int currency_idx = -1;
    for (int i = 0; i < currency_count; ++i) {
        if (currencies[i].currency_id == chosen_currency_id) {
            currency_idx = i;
            break;
        }
    }
    if (currency_idx == -1) {
        fprintf(stderr, "Could not find currency index.\n");
        free(currencies);
        free(events);
        return;
    }

    int store_item_count;
    struct store_item *store_items = get_store_items_by_event(db, &store_item_count, chosen_event_id);

    int s_id_width = 10;
    int desc_width = 80;
    int cost_width = 20;
    int stock_width = 10;
    int category_width = 20;

    print_top_border(5, s_id_width, desc_width, cost_width, stock_width, category_width);
    print_table_row(5, "ID", s_id_width, "Description", desc_width, "Cost", cost_width, "Stock", stock_width, "Category", category_width);
    print_row_separator(5, s_id_width, desc_width, cost_width, stock_width, category_width);

    for (int i = 0; i < store_item_count; ++i) {
        char id_str[10];
        snprintf(id_str, sizeof(id_str), "%d", store_items[i].item_id);

        char cost_str[20];
        snprintf(cost_str, sizeof(cost_str), "%d %s", store_items[i].cost, currencies[currency_idx].symbol);

        char stock_str[10];
        if (store_items[i].stock == -1) {
            snprintf(stock_str, sizeof(stock_str), "INF");
        }
        else snprintf(stock_str, sizeof(stock_str), "%d", store_items[i].stock);

        print_table_row(5, id_str, s_id_width, store_items[i].item_description, desc_width, cost_str, cost_width, stock_str, stock_width, store_items[i].category, category_width);
    }
    print_bottom_border(5, s_id_width, desc_width, cost_width, stock_width, category_width);

    printf("Enter item to buy: ");
    int chosen_item_id;
    scanf("%d", &chosen_item_id);
    flush_input_buffer();

    int item_idx = -1;
    for (int i = 0; i < store_item_count; ++i) {
        if (chosen_item_id == store_items[i].item_id) {
            item_idx = i;
            break;
        }
    }
    if (item_idx == -1) {
        fprintf(stderr, "Could not find item index.\n");
        free(currencies);
        free(events);
        free(store_items);
        return;
    }

    if (store_items[item_idx].stock == 0) {
        fprintf(stderr, "Item out of stock.\n");
        free(currencies);
        free(events);
        free(store_items);
        return; 
    }

    if (currencies[currency_idx].balance < store_items[item_idx].cost) {
        fprintf(stderr, "Insufficient balance.\n");
        free(currencies);
        free(events);
        free(store_items);
        return;
    }

    sqlite3_bind_int(stmt_update_store_stock, 1, chosen_item_id);
    sqlite3_bind_int(stmt_update_store_stock, 2, chosen_event_id);

    rc = sqlite3_step(stmt_update_store_stock);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Error in updating stock: %s\n", sqlite3_errmsg(db));
        sqlite3_reset(stmt_update_store_stock);
        free(currencies);
        free(events);
        free(store_items);
        return;
    }

    sqlite3_reset(stmt_update_store_stock);

    sqlite3_bind_int(stmt_update_balance, 1, -store_items[item_idx].cost);
    sqlite3_bind_int(stmt_update_balance, 2, chosen_currency_id);

    rc = sqlite3_step(stmt_update_balance);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Error in updating balance: %s\n", sqlite3_errmsg(db));
        sqlite3_reset(stmt_update_balance);
        free(currencies);
        free(events);
        free(store_items);
        return;
    }

    sqlite3_reset(stmt_update_balance);

    printf("Current balance\n");
    currencies = NULL;
    currencies = get_currencies(db, &currency_count);
    print_currency_table(currencies, currency_count);

    free(currencies);
    free(events);
    free(store_items);
}

struct task * get_all_tasks_of_an_event(sqlite3 *db, int *task_count, int event_id) {
    int rc;
    struct task *tasks = NULL;
    *task_count = 0;
    int task_capacity = 10;
    tasks = malloc(task_capacity * sizeof(struct task));
    if (!tasks) {
        fprintf(stderr, "Unable to allocate memory for tasks.\n");
        return NULL;
    }

    sqlite3_bind_int(stmt_select_all_tasks_of_an_event, 1, event_id);

    while ((rc = sqlite3_step(stmt_select_all_tasks_of_an_event)) == SQLITE_ROW) {
        if (*task_count >= task_capacity) {
            task_capacity *= 2;
            struct task *new_tasks = realloc(tasks, task_capacity * sizeof(struct task));
            if (!new_tasks) {
                fprintf(stderr, "Unable to reallocate tasks.\n");
                sqlite3_reset(stmt_select_all_tasks_of_an_event);
                free(tasks);
                return NULL;
            }
            tasks = new_tasks;
        }

        tasks[*task_count].event_id = sqlite3_column_int(stmt_select_all_tasks_of_an_event, 0);
        tasks[*task_count].task_id = sqlite3_column_int(stmt_select_all_tasks_of_an_event, 1);
        const char *task_name = (const char *)sqlite3_column_text(stmt_select_all_tasks_of_an_event, 2);
        strncpy(tasks[*task_count].task_description, task_name, sizeof(tasks[*task_count].task_description) - 1);
        tasks[*task_count].task_description[sizeof(tasks[*task_count].task_description) - 1] = '\0';
        tasks[*task_count].currency_amount = sqlite3_column_int(stmt_select_all_tasks_of_an_event, 3);
        tasks[*task_count].is_completed = sqlite3_column_int(stmt_select_all_tasks_of_an_event, 4);

        (*task_count)++;
    }

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Error fetching tasks: %s\n", sqlite3_errmsg(db));
        free(tasks);
        sqlite3_reset(stmt_select_all_tasks_of_an_event);
        return NULL;
    }

    sqlite3_reset(stmt_select_all_tasks_of_an_event);
    return tasks;
}

void list_events_and_tasks(sqlite3 *db) {
    int event_count;
    struct event *events = get_active_events(db, &event_count);

    int currency_count;
    struct currency *currencies = get_currencies(db, &currency_count);

    int id_width = 10;
    int name_desc_width = 80;
    int time_width = 20;

    print_top_border(4, id_width, name_desc_width, time_width, time_width);
    print_table_row(4, "EID/TID", id_width, "Name/Description", name_desc_width, "Start Time/Currency", time_width, "End Time/Completed", time_width);
    print_row_separator(4, id_width, name_desc_width, time_width, time_width);

    for (int i = 0; i < event_count; ++i) {

        int currency_idx = -1;
        for (int j = 0; j < currency_count; ++j) {
            if (currencies[j].currency_id == events[i].currency_id) {
                currency_idx = j;
                break;
            }
        }
        if (currency_idx == -1) {
            fprintf(stderr, "Could not find currency index.\n");
            free(events);
            free(currencies);
            return;
        }

        int task_count;
        struct task *tasks = get_all_tasks_of_an_event(db, &task_count, events[0].event_id);

        char e_id_str[10];
        snprintf(e_id_str, sizeof(e_id_str), "%d", events[i].event_id);

        char start_time_str[21] = "N/A";
        char end_time_str[21] = "N/A";

        if (events[i].start_time != -1 && events[i].end_time != -1) {
            struct tm *start_tm = localtime(&events[i].start_time);
            struct tm *end_tm = localtime(&events[i].end_time);

            strftime(start_time_str, sizeof(start_time_str), "%Y-%m-%d %H:%M:%S", start_tm);
            strftime(end_time_str, sizeof(end_time_str), "%Y-%m-%d %H:%M:%S", end_tm);
        }

        print_table_row(4, e_id_str, id_width, events[i].event_name, name_desc_width, start_time_str, time_width, end_time_str, time_width);
        print_row_separator(4, id_width, name_desc_width, time_width, time_width);

        for (int j = 0; j < task_count; ++j) {
            char t_id_str[10];
            snprintf(t_id_str, sizeof(t_id_str), "%d", tasks[j].task_id);

            char curr_str[20];
            snprintf(curr_str, sizeof(curr_str), "%d %s", tasks[j].currency_amount, currencies[currency_idx].symbol);

            char completed[20];
            if (tasks[j].is_completed == 0) {
                snprintf(completed, sizeof(completed), "No");
            } else snprintf(completed, sizeof(completed), "Yes");

            print_table_row(4, t_id_str, id_width, tasks[j].task_description, name_desc_width, curr_str, time_width, completed, time_width);
        }
        
        if (i < event_count - 1)
            print_row_separator(4, id_width, name_desc_width, time_width, time_width);
    }

    print_bottom_border(4, id_width, name_desc_width, time_width, time_width);
}

void list_stats(sqlite3 *db) {
    int currency_count;
    struct currency *currencies = get_currencies(db, &currency_count);

    printf("Current balance\n");
    print_currency_table(currencies, currency_count);

    free(currencies);
}