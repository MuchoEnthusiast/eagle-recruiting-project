#include <stdio.h>
#include <iostream>
#include <string>
#include <chrono>
#include <fstream>
#include <cstring>
#include <vector>
#include <ranges>
#include <map>
#include <filesystem>

using namespace std;
using msg_id = string;

extern "C" {
    #include "fake_receiver.h"
}

enum class State {
    Idle,
    Run
};

struct message_stats {
    int id_count;
    int64_t old_timestamp;
    int64_t total_time;
    int64_t avg_time;
};

struct session_data {
    string csv_filename;
    map<msg_id, message_stats> session_stats;  
};


void create_directory_for_file(const string& filepath) {
    filesystem::path path_obj(filepath);
    filesystem::path dir_path = path_obj.parent_path();
    
    if (!dir_path.empty() && !filesystem::exists(dir_path)) {
        filesystem::create_directories(dir_path);
    }
}


vector<string> split(const string& str, char delimiter) {
    vector<string> tokens;
    string token;
    size_t start = 0;
    size_t end = str.find(delimiter);
    
    while (end != string::npos) {
        token = str.substr(start, end - start);
        tokens.push_back(token);
        start = end + 1;
        end = str.find(delimiter, start);
    }
    tokens.push_back(str.substr(start));
    return tokens;
}

void update_statistics(const char* message, int64_t timestamp, map<msg_id, message_stats>& statistics) {
    // Split the message by the symbol #
    vector<string> splitted = split(string(message), '#');
    
    if (splitted.size() < 2) {
        return;  // Invalid message format
    }
    
    string id = splitted[0];
    string payload = splitted[1];

    if (payload.length() % 2 == 0) {
        if (statistics.find(id) == statistics.end()) {
            // Initialize new entry
            message_stats stat;
            stat.id_count = 1;
            stat.old_timestamp = timestamp;
            stat.total_time = 0;
            stat.avg_time = 0;
            statistics[id] = stat;
        } else {
            // Update existing entry
            statistics[id].id_count++;
            statistics[id].total_time += (timestamp - statistics[id].old_timestamp);
            if (statistics[id].id_count > 1) {
                statistics[id].avg_time = statistics[id].total_time / (statistics[id].id_count - 1);
            }
            statistics[id].old_timestamp = timestamp;
        }
    }
}

void save_statistics_to_csv(const session_data& session_data) {
    create_directory_for_file(session_data.csv_filename);
    ofstream stats_file(session_data.csv_filename, ios::trunc);
    if (!stats_file.is_open()) {
        cerr << "Failed to open " << session_data.csv_filename << endl;
        return;
    }
    
    // Write header
    stats_file << "ID,number_of_messages,mean_time" << endl;
    
    // Write all statistics
    for (const auto& [id, stats] : session_data.session_stats) {
        stats_file << id << ","
                   << stats.id_count << ","
                   << stats.avg_time << endl;
    }
    
    stats_file.close();
}

void finite_state_machine(const char* filepath, char message[MAX_CAN_MESSAGE_SIZE]) {
    int a = open_can(filepath);
    cout << "is can open: " << a << endl;
    
    int file_id = 0;
    int i = 0;
    State state = State::Idle;
    ofstream file;
    session_data current;      
    while (i > -1) {
        i = can_receive(message);

        if (i <= 0) { 
            continue;
        }

        cout << "how many bytes received? " << i << endl;
        cout << "message: " << message << endl;

        // State transition: Idle -> Run
        if (((strcmp(message, "0A0#6601") == 0) || (strcmp(message, "0A0#FF01") == 0)) && state == State::Idle) {
            state = State::Run;
            
            // Reset session data for new session
            current = session_data();             
            current.csv_filename = "Statistics/Stats_Reg" + to_string(file_id) + ".csv";
            current.session_stats.clear();

            string file_name = "Registers/Reg" + to_string(file_id) + ".log";

            create_directory_for_file(file_name);
            file.open(file_name, ios::trunc);

            auto now = chrono::system_clock::now();
            int64_t timestamp = chrono::duration_cast<chrono::milliseconds>(now.time_since_epoch()).count();

            update_statistics(message, timestamp, current.session_stats);
            
            file << timestamp << " " << message << endl;
            file.flush();
        } 
        // State transition: Run -> Idle
        else if (strcmp(message, "0A0#66FF") == 0 && state == State::Run) {
            state = State::Idle;
            
            // Update statistics for the stop message
            auto now = chrono::system_clock::now();
            int64_t timestamp = chrono::duration_cast<chrono::milliseconds>(
                now.time_since_epoch()).count();
            update_statistics(message, timestamp, current.session_stats);
            
            if (file.is_open()) {
                file.close();
                cout << "Closed file: Reg" << file_id << ".log" << endl;
            }
            
            save_statistics_to_csv(current);
            file_id++;
        }
        // Log messages in Run state (excluding state transition messages)
        else if (state == State::Run && file.is_open()) {
            // Avoid duplicating the transition message that was already written
            if (strcmp(message, "0A0#6601") != 0 && 
                strcmp(message, "0A0#FF01") != 0 && 
                strcmp(message, "0A0#66FF") != 0) {
                
                // Get current timestamp in milliseconds since epoch
                auto now = chrono::system_clock::now();
                int64_t timestamp = chrono::duration_cast<chrono::milliseconds>(
                    now.time_since_epoch()).count();

                update_statistics(message, timestamp, current.session_stats);
                
                file << timestamp << " " << message << endl;
                file.flush();
            }
        }
        
        cout << "current state is: " << static_cast<int>(state) << endl;
    }

    if (file.is_open()) {
        file.close();
    }
    close_can();
}

int main(void) {

    char message[MAX_CAN_MESSAGE_SIZE];
    const char* filepath = "candump.log";
    
    message[0] = '\0';  
    
    finite_state_machine(filepath, message);
    
    return 0;
}
