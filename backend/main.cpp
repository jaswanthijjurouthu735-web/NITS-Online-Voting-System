
#define ASIO_STANDALONE

#include "include/crow_all.h"

#include <sqlite3.h>
#include <curl/curl.h>

#include <iostream>
#include <string>
#include <fstream>
#include <random>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <cctype>
#include <sstream>
#include <iomanip>
#include <algorithm>

#undef DELETE

using namespace std;

// =====================================================
// GLOBAL VARIABLES
// =====================================================

sqlite3* db = nullptr;
string brevoApiKey = "";
string adminPassword = "";

struct SessionData { int studentId = 0; bool admin = false; };
unordered_map<string, SessionData> sessions;
unordered_map<string, pair<string, chrono::steady_clock::time_point>> verifiedEmails;
mutex sessionMutex;

struct OTPData
{
    string code;
    chrono::steady_clock::time_point expiry;
    int attempts = 0;
};

unordered_map<string, OTPData> otpStorage;
mutex otpMutex;

// Prevent repeated OTP emails to the same address.
unordered_map<string, chrono::steady_clock::time_point> otpRequestTimes;
constexpr int OTP_RESEND_COOLDOWN_SECONDS = 60;

// =====================================================
// LOAD ENV FILE
// =====================================================

bool loadEnvFile()
{
    ifstream envFile(".env");

    if (!envFile.is_open())
    {
        cerr << "Could not open .env file." << endl;
        return false;
    }

    string line;

    while (getline(envFile, line))
    {
        size_t start = line.find_first_not_of(" \t\r\n");

        if (start == string::npos)
            continue;

        line = line.substr(start);

        if (line[0] == '#')
            continue;

        size_t pos = line.find('=');

        if (pos == string::npos)
            continue;

        string key = line.substr(0, pos);
        string value = line.substr(pos + 1);

        size_t end = key.find_last_not_of(" \t\r\n");

        if (end != string::npos)
            key = key.substr(0, end + 1);

        size_t valueStart = value.find_first_not_of(" \t\r\n");

        if (valueStart == string::npos)
            value = "";
        else
            value = value.substr(valueStart);

        size_t valueEnd = value.find_last_not_of(" \t\r\n");

        if (valueEnd != string::npos)
            value = value.substr(0, valueEnd + 1);

        if (value.size() >= 2 &&
            ((value.front() == '"' && value.back() == '"') ||
             (value.front() == '\'' && value.back() == '\'')))
        {
            value = value.substr(1, value.size() - 2);
        }

        if (key == "BREVO_API_KEY")
            brevoApiKey = value;
        if (key == "ADMIN_PASSWORD")
            adminPassword = value;
    }

    envFile.close();

    if (brevoApiKey.empty())
    {
        cerr << "BREVO_API_KEY missing in .env." << endl;
        return false;
    }

    if (adminPassword.empty())
    {
        cerr << "ADMIN_PASSWORD missing in .env." << endl;
        return false;
    }

    cout << "Brevo API key loaded successfully." << endl;

    return true;
}

// =====================================================
// JSON ESCAPING
// =====================================================

string jsonEscape(const string& input)
{
    string output;

    for (char c : input)
    {
        switch (c)
        {
            case '"':
                output += "\\\"";
                break;

            case '\\':
                output += "\\\\";
                break;

            case '\n':
                output += "\\n";
                break;

            case '\r':
                output += "\\r";
                break;

            case '\t':
                output += "\\t";
                break;

            default:
                if (static_cast<unsigned char>(c) < 32)
                    output += ' ';
                else
                    output += c;
        }
    }

    return output;
}

// =====================================================
// BREVO EMAIL FUNCTION
// =====================================================

bool sendBrevoEmail(
    const string& recipient,
    const string& subject,
    const string& message
)
{
    CURL* curl = curl_easy_init();

    if (!curl)
    {
        cerr << "Could not initialize cURL." << endl;
        return false;
    }

    string payload =
        "{"
        "\"sender\":{"
            "\"name\":\"NITS VOTING SYSTEM\","
            "\"email\":\"jaswanthijjurouthu735@gmail.com\""
        "},"
        "\"to\":[{"
            "\"email\":\"" + jsonEscape(recipient) + "\""
        "}],"
        "\"subject\":\"" + jsonEscape(subject) + "\","
        "\"textContent\":\"" + jsonEscape(message) + "\""
        "}";

    struct curl_slist* headers = nullptr;

    string apiHeader = "api-key: " + brevoApiKey;

    headers = curl_slist_append(
        headers,
        apiHeader.c_str()
    );

    headers = curl_slist_append(
        headers,
        "Content-Type: application/json"
    );

    headers = curl_slist_append(
        headers,
        "Accept: application/json"
    );

    curl_easy_setopt(
        curl,
        CURLOPT_URL,
        "https://api.brevo.com/v3/smtp/email"
    );

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);

    curl_easy_setopt(
        curl,
        CURLOPT_POSTFIELDS,
        payload.c_str()
    );

    // Capture Brevo response body

    string responseBody;

    curl_easy_setopt(
        curl,
        CURLOPT_WRITEFUNCTION,
        +[](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t
        {
            size_t total = size * nmemb;

            string* response = static_cast<string*>(userdata);

            response->append(ptr, total);

            return total;
        }
    );

    curl_easy_setopt(
        curl,
        CURLOPT_WRITEDATA,
        &responseBody
    );

    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode result = curl_easy_perform(curl);

    long statusCode = 0;

    if (result == CURLE_OK)
    {
        curl_easy_getinfo(
            curl,
            CURLINFO_RESPONSE_CODE,
            &statusCode
        );
    }
    else
    {
        cerr << "cURL error: "
             << curl_easy_strerror(result)
             << endl;
    }

    // Print Brevo response for debugging

    cerr << "Brevo HTTP status: " << statusCode << endl;
    cerr << "Brevo response: " << responseBody << endl;

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (
        result == CURLE_OK &&
        statusCode >= 200 &&
        statusCode < 300
    )
    {
        cout << "Email sent successfully!" << endl;
        return true;
    }

    return false;
}

// =====================================================
// GENERATE SIX DIGIT OTP
// =====================================================

string generateOTP()
{
    static random_device rd;
    static mt19937 generator(rd());

    uniform_int_distribution<int> distribution(100000, 999999);

    return to_string(distribution(generator));
}

// =====================================================
// CORS
// =====================================================

struct CORS
{
    struct context {};

    void before_handle(
        crow::request& req,
        crow::response& res,
        context& ctx
    )
    {
        if (req.method == crow::HTTPMethod::OPTIONS)
        {
            res.code = 204;

            res.add_header(
                "Access-Control-Allow-Origin",
                "http://127.0.0.1:5500"
            );

            res.add_header(
                "Access-Control-Allow-Methods",
                "GET, POST, PUT, DELETE, OPTIONS"
            );

            res.add_header(
                "Access-Control-Allow-Headers",
                "Content-Type, Authorization"
            );

            res.end();
        }
    }

    void after_handle(
        crow::request& req,
        crow::response& res,
        context& ctx
    )
    {
        res.add_header(
            "Access-Control-Allow-Origin",
            "http://127.0.0.1:5500"
        );

        res.add_header(
            "Access-Control-Allow-Methods",
            "GET, POST, PUT, DELETE, OPTIONS"
        );

        res.add_header(
            "Access-Control-Allow-Headers",
            "Content-Type, Authorization"
        );
    }
};

// =====================================================
// DATABASE HELPER
// =====================================================

bool executeSQL(const char* sql)
{
    char* errorMessage = nullptr;

    int result = sqlite3_exec(
        db,
        sql,
        nullptr,
        nullptr,
        &errorMessage
    );

    if (result != SQLITE_OK)
    {
        cerr << "Database error: "
             << (errorMessage ? errorMessage : "Unknown error")
             << endl;

        sqlite3_free(errorMessage);

        return false;
    }

    return true;
}

// =====================================================
// DATABASE SETUP
// =====================================================

bool setupDatabase()
{
    const char* sql = R"(

        CREATE TABLE IF NOT EXISTS candidates (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            position TEXT NOT NULL,
            department TEXT NOT NULL,
            manifesto TEXT DEFAULT '',
            achievements TEXT DEFAULT ''
        );

        CREATE TABLE IF NOT EXISTS students (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            email TEXT NOT NULL UNIQUE COLLATE NOCASE,
            scholar_id TEXT NOT NULL UNIQUE,
            department TEXT NOT NULL,
            password_hash TEXT,
            email_verified INTEGER DEFAULT 0,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP
        );

        CREATE TABLE IF NOT EXISTS admins (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT NOT NULL UNIQUE,
            password_hash TEXT NOT NULL,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP
        );

        CREATE TABLE IF NOT EXISTS elections (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            start_time TEXT NOT NULL,
            end_time TEXT NOT NULL,
            status TEXT NOT NULL DEFAULT 'draft'
                CHECK(status IN (
                    'draft',
                    'scheduled',
                    'ongoing',
                    'completed'
                )),
            results_published INTEGER DEFAULT 0,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP
        );

        CREATE TABLE IF NOT EXISTS votes (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            student_id INTEGER NOT NULL,
            election_id INTEGER NOT NULL,
            candidate_id INTEGER NOT NULL,
            position TEXT NOT NULL,
            voted_at TEXT DEFAULT CURRENT_TIMESTAMP,

            FOREIGN KEY(student_id) REFERENCES students(id),
            FOREIGN KEY(election_id) REFERENCES elections(id),
            FOREIGN KEY(candidate_id) REFERENCES candidates(id),

            UNIQUE(student_id, election_id, position)
        );

        CREATE TABLE IF NOT EXISTS otp_codes (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            email TEXT NOT NULL COLLATE NOCASE,
            code_hash TEXT NOT NULL,
            expires_at TEXT NOT NULL,
            attempts INTEGER DEFAULT 0,
            verified INTEGER DEFAULT 0,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP
        );

        CREATE INDEX IF NOT EXISTS idx_students_email
        ON students(email);

        CREATE INDEX IF NOT EXISTS idx_students_scholar_id
        ON students(scholar_id);

        CREATE INDEX IF NOT EXISTS idx_votes_election
        ON votes(election_id);

        CREATE INDEX IF NOT EXISTS idx_votes_candidate
        ON votes(candidate_id);

        CREATE INDEX IF NOT EXISTS idx_otp_email
        ON otp_codes(email);

    )";

    if (!executeSQL(sql))
        return false;

    cout << "All database tables are ready!" << endl;

    return true;
}

// =====================================================
// SAMPLE CANDIDATE
// =====================================================

bool insertSampleCandidates()
{
    const char* sql = R"(
        INSERT INTO candidates
        (name, position, department, manifesto, achievements)

        SELECT
            'Rahul Sharma',
            'GS',
            'CSE',
            'Improve student facilities',
            ''

        WHERE NOT EXISTS (
            SELECT 1 FROM candidates
            WHERE name = 'Rahul Sharma'
            AND position = 'GS'
        );
    )";

    return executeSQL(sql);
}

// =====================================================
// AUTHENTICATION AND DATABASE HELPERS
// =====================================================

string randomToken(size_t length = 48)
{
    static const char alphabet[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static random_device rd;
    static mt19937 gen(rd());
    uniform_int_distribution<size_t> pick(0, sizeof(alphabet) - 2);
    string token;
    for (size_t i = 0; i < length; ++i) token += alphabet[pick(gen)];
    return token;
}

string requestToken(const crow::request& req)
{
    string value = req.get_header_value("Authorization");
    const string prefix = "Bearer ";
    if (value.rfind(prefix, 0) == 0) return value.substr(prefix.size());
    return "";
}

bool sessionFor(const crow::request& req, SessionData& out)
{
    string token = requestToken(req);
    if (token.empty()) return false;
    lock_guard<mutex> lock(sessionMutex);
    auto it = sessions.find(token);
    if (it == sessions.end()) return false;
    out = it->second;
    return true;
}

bool adminFor(const crow::request& req)
{
    SessionData session;
    return sessionFor(req, session) && session.admin;
}

int activeElectionId()
{
    sqlite3_stmt* stmt = nullptr;
    int id = 0;
    if (sqlite3_prepare_v2(db, "SELECT id FROM elections ORDER BY id DESC LIMIT 1", -1, &stmt, nullptr) == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW)
        id = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return id;
}

string columnString(sqlite3_stmt* stmt, int col)
{
    const unsigned char* value = sqlite3_column_text(stmt, col);
    return value ? reinterpret_cast<const char*>(value) : "";
}

// =====================================================
// MAIN
// =====================================================

int main()
{
    // Load API key

    if (!loadEnvFile())
        return 1;

    // Initialize cURL

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
    {
        cerr << "cURL initialization failed." << endl;
        return 1;
    }

    // Open database

    if (sqlite3_open("nits_voting.db", &db) != SQLITE_OK)
    {
        cerr << "Failed to open database." << endl;

        if (db)
            sqlite3_close(db);

        curl_global_cleanup();

        return 1;
    }

    executeSQL("PRAGMA foreign_keys = ON;");

    sqlite3_busy_timeout(db, 5000);

    if (!setupDatabase() || !insertSampleCandidates())
    {
        sqlite3_close(db);
        curl_global_cleanup();
        return 1;
    }

    executeSQL("INSERT INTO elections(name,start_time,end_time,status) SELECT 'NITS Gymkhana Election', datetime('now'), datetime('now','+1 day'), 'draft' WHERE NOT EXISTS (SELECT 1 FROM elections);");

    cout << "SQLite database and tables ready!" << endl;

    crow::App<CORS> app;

    // =================================================
    // HOME
    // =================================================

    CROW_ROUTE(app, "/")
    ([] {
        return "NITS Online Voting System Backend is running!";
    });

    // =================================================
    // HEALTH
    // =================================================

    CROW_ROUTE(app, "/api/health")
    ([] {
        crow::json::wvalue response;

        response["status"] = "success";
        response["database"] = "connected";

        return response;
    });

    // =================================================
    // SEND OTP
    // =================================================

    CROW_ROUTE(app, "/api/send-otp")
    .methods(crow::HTTPMethod::POST)
    ([&](const crow::request& req) {

        crow::json::wvalue response;

        auto body = crow::json::load(req.body);

        if (!body || !body.has("email"))
        {
            response["status"] = "error";
            response["message"] = "Email is required.";

            return crow::response(400, response);
        }

        string email = body["email"].s();
        transform(email.begin(), email.end(), email.begin(), [](unsigned char c){ return static_cast<char>(tolower(c)); });

        size_t at = email.find('@');
        string domain = at == string::npos ? "" : email.substr(at + 1);
        bool nitsDomain = domain == "nits.ac.in" || (domain.size() > 11 && domain.substr(domain.size() - 11) == ".nits.ac.in");

        if (email.empty() ||
            at == string::npos || !nitsDomain)
        {
            response["status"] = "error";
            response["message"] = "Use your NITS student email address (for example, name@nits.ac.in).";

            return crow::response(400, response);
        }

        // Enforce a server-side resend cooldown per email address.
        // Reserve the cooldown before sending so simultaneous requests cannot
        // trigger multiple emails. If delivery fails, the reservation is cleared.
        {
            lock_guard<mutex> lock(otpMutex);
            const auto now = chrono::steady_clock::now();

            // Remove expired cooldown entries to keep this map bounded over time.
            for (auto it = otpRequestTimes.begin(); it != otpRequestTimes.end(); )
            {
                if (chrono::duration_cast<chrono::seconds>(now - it->second).count() >= OTP_RESEND_COOLDOWN_SECONDS)
                    it = otpRequestTimes.erase(it);
                else
                    ++it;
            }

            auto previous = otpRequestTimes.find(email);
            if (previous != otpRequestTimes.end())
            {
                const auto elapsed = chrono::duration_cast<chrono::seconds>(now - previous->second).count();
                if (elapsed < OTP_RESEND_COOLDOWN_SECONDS)
                {
                    const int waitSeconds = OTP_RESEND_COOLDOWN_SECONDS - static_cast<int>(elapsed);
                    response["status"] = "error";
                    response["message"] = "Please wait " + to_string(waitSeconds) + " seconds before requesting another OTP.";
                    response["retry_after"] = waitSeconds;
                    crow::response limited(429, response);
                    limited.add_header("Retry-After", to_string(waitSeconds));
                    return limited;
                }
            }

            otpRequestTimes[email] = now;
        }

        // Generate OTP

        string otp = generateOTP();

        // Send email first

        string emailMessage =
            "Your NITS Voting System verification code is: " +
            otp +
            "\n\nThis OTP expires in 5 minutes."
            "\nDo not share this code with anyone.";

        bool sent = sendBrevoEmail(
            email,
            "NITS Voting System - OTP Verification",
            emailMessage
        );

        if (!sent)
        {
            // Allow a retry if the email provider failed to deliver the OTP.
            {
                lock_guard<mutex> lock(otpMutex);
                otpRequestTimes.erase(email);
            }

            response["status"] = "error";
            response["message"] = "Could not send OTP email.";

            return crow::response(502, response);
        }

        // Store OTP in server memory

        {
            lock_guard<mutex> lock(otpMutex);

            OTPData data;

            data.code = otp;
            data.expiry =
                chrono::steady_clock::now() +
                chrono::minutes(5);

            data.attempts = 0;

            otpStorage[email] = data;
        }

        response["status"] = "success";
        response["message"] = "OTP sent successfully.";
        response["expires_in"] = 300;

        return crow::response(200, response);
    });

    // =================================================
    // VERIFY OTP
    // =================================================

    CROW_ROUTE(app, "/api/verify-otp")
    .methods(crow::HTTPMethod::POST)
    ([&](const crow::request& req) {

        crow::json::wvalue response;

        auto body = crow::json::load(req.body);

        if (!body ||
            !body.has("email") ||
            !body.has("otp"))
        {
            response["status"] = "error";
            response["message"] =
                "Email and OTP are required.";

            return crow::response(400, response);
        }

        string email = body["email"].s();
        string otp = body["otp"].s();

        if (otp.size() != 6)
        {
            response["status"] = "error";
            response["message"] = "OTP must contain 6 digits.";

            return crow::response(400, response);
        }

        for (char c : otp)
        {
            if (!isdigit(static_cast<unsigned char>(c)))
            {
                response["status"] = "error";
                response["message"] = "Invalid OTP format.";

                return crow::response(400, response);
            }
        }

        lock_guard<mutex> lock(otpMutex);

        auto it = otpStorage.find(email);

        if (it == otpStorage.end())
        {
            response["status"] = "error";
            response["message"] =
                "No OTP found. Please request a new OTP.";

            return crow::response(400, response);
        }

        OTPData& data = it->second;

        if (chrono::steady_clock::now() > data.expiry)
        {
            otpStorage.erase(it);

            response["status"] = "error";
            response["message"] =
                "OTP expired. Please request a new OTP.";

            return crow::response(400, response);
        }

        if (data.attempts >= 5)
        {
            otpStorage.erase(it);

            response["status"] = "error";
            response["message"] =
                "Maximum attempts exceeded. Request a new OTP.";

            return crow::response(429, response);
        }

        if (data.code != otp)
        {
            data.attempts++;

            response["status"] = "error";
            response["message"] = "Incorrect OTP.";

            return crow::response(400, response);
        }

        // Correct OTP: issue a short-lived registration token or student session.
        otpStorage.erase(it);
        sqlite3_stmt* studentStmt = nullptr;
        bool exists = false;
        int studentId = 0;
        if (sqlite3_prepare_v2(db, "SELECT id FROM students WHERE email=? COLLATE NOCASE AND email_verified=1", -1, &studentStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(studentStmt, 1, email.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(studentStmt) == SQLITE_ROW) { exists = true; studentId = sqlite3_column_int(studentStmt, 0); }
        }
        sqlite3_finalize(studentStmt);
        response["status"] = "success";
        response["verified"] = true;
        response["message"] = "Email verified successfully.";
        if (exists) {
            string token = randomToken();
            { lock_guard<mutex> sessionLock(sessionMutex); sessions[token] = {studentId, false}; }
            response["registered"] = true;
            response["token"] = token;
            response["student_id"] = studentId;
        } else {
            string verifyToken = randomToken();
            { lock_guard<mutex> sessionLock(sessionMutex); verifiedEmails[verifyToken] = {email, chrono::steady_clock::now() + chrono::minutes(10)}; }
            response["registered"] = false;
            response["verification_token"] = verifyToken;
        }
        return crow::response(200, response);
    });

    // =================================================
    // TEST EMAIL
    // =================================================

    CROW_ROUTE(app, "/api/test-email")
    .methods(crow::HTTPMethod::POST)
    ([&](const crow::request& req) {

        crow::json::wvalue response;

        auto body = crow::json::load(req.body);

        if (!body || !body.has("email"))
        {
            response["status"] = "error";
            response["message"] = "Email is required.";

            return crow::response(400, response);
        }

        string email = body["email"].s();

        bool sent = sendBrevoEmail(
            email,
            "NITS Voting System - Test",
            "Your Brevo email integration is working."
        );

        if (!sent)
        {
            response["status"] = "error";
            response["message"] = "Email could not be sent.";

            return crow::response(502, response);
        }

        response["status"] = "success";
        response["message"] = "Test email sent successfully.";

        return crow::response(200, response);
    });

    // =================================================
    // GET CANDIDATES
    // =================================================

    CROW_ROUTE(app, "/api/candidates")
    ([] {

        crow::json::wvalue response;
        crow::json::wvalue::list candidates;

        const char* sql =
            "SELECT id, name, position, department, manifesto, achievements "
            "FROM candidates ORDER BY id;";

        sqlite3_stmt* stmt = nullptr;

        if (sqlite3_prepare_v2(
            db, sql, -1, &stmt, nullptr
        ) != SQLITE_OK)
        {
            response["status"] = "error";
            response["message"] = "Database error.";

            return response;
        }

        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            crow::json::wvalue candidate;

            candidate["id"] = sqlite3_column_int(stmt, 0);

            for (int i = 1; i <= 5; i++)
            {
                const unsigned char* value =
                    sqlite3_column_text(stmt, i);

                string field = value
                    ? reinterpret_cast<const char*>(value)
                    : "";

                if (i == 1) candidate["name"] = field;
                if (i == 2) candidate["position"] = field;
                if (i == 3) candidate["department"] = field;
                if (i == 4) candidate["manifesto"] = field;
                if (i == 5) candidate["achievements"] = field;
            }

            candidates.push_back(std::move(candidate));
        }

        sqlite3_finalize(stmt);

        response["status"] = "success";
        response["candidates"] = std::move(candidates);

        return response;
    });

    // =================================================
    // ADD CANDIDATE
    // =================================================

    CROW_ROUTE(app, "/api/candidates")
    .methods(crow::HTTPMethod::POST)
    ([&](const crow::request& req) {

        crow::json::wvalue response;
        if (!adminFor(req)) { response["status"] = "error"; response["message"] = "Administrator authentication required."; return crow::response(401, response); }

        auto body = crow::json::load(req.body);

        if (!body ||
            !body.has("name") ||
            !body.has("position") ||
            !body.has("manifesto"))
        {
            response["status"] = "error";
            response["message"] = "Required fields are missing.";

            return crow::response(400, response);
        }

        string name = body["name"].s();
        string position = body["position"].s();
        string manifesto = body["manifesto"].s();

        string department = "Not specified";
        string achievements = "";

        if (body.has("department"))
            department = body["department"].s();

        if (body.has("achievements"))
            achievements = body["achievements"].s();

        if (name.empty() || position.empty() || manifesto.empty())
        {
            response["status"] = "error";
            response["message"] = "Fields cannot be empty.";

            return crow::response(400, response);
        }

        const char* sql =
            "INSERT INTO candidates "
            "(name, position, department, manifesto, achievements) "
            "VALUES (?, ?, ?, ?, ?);";

        sqlite3_stmt* stmt = nullptr;

        if (sqlite3_prepare_v2(
            db, sql, -1, &stmt, nullptr
        ) != SQLITE_OK)
        {
            response["status"] = "error";
            response["message"] = "Database error.";

            return crow::response(500, response);
        }

        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, position.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, department.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, manifesto.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, achievements.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) != SQLITE_DONE)
        {
            sqlite3_finalize(stmt);

            response["status"] = "error";
            response["message"] = "Could not save candidate.";

            return crow::response(500, response);
        }

        int newId = static_cast<int>(
            sqlite3_last_insert_rowid(db)
        );

        sqlite3_finalize(stmt);

        response["status"] = "success";
        response["message"] = "Candidate saved successfully.";
        response["id"] = newId;

        return crow::response(201, response);
    });

    // =================================================
    // UPDATE CANDIDATE
    // =================================================

    CROW_ROUTE(app, "/api/candidates/<int>")
    .methods(crow::HTTPMethod::PUT)
    ([&](const crow::request& req, int id) {

        crow::json::wvalue response;
        if (!adminFor(req)) { response["status"] = "error"; response["message"] = "Administrator authentication required."; return crow::response(401, response); }

        auto body = crow::json::load(req.body);

        if (id <= 0 || !body ||
            !body.has("name") ||
            !body.has("position") ||
            !body.has("manifesto"))
        {
            response["status"] = "error";
            response["message"] = "Invalid ID or missing fields.";

            return crow::response(400, response);
        }

        string name = body["name"].s();
        string position = body["position"].s();
        string manifesto = body["manifesto"].s();

        string department = "Not specified";
        string achievements = "";

        if (body.has("department"))
            department = body["department"].s();

        if (body.has("achievements"))
            achievements = body["achievements"].s();

        if (name.empty() || position.empty() || manifesto.empty())
        {
            response["status"] = "error";
            response["message"] = "Fields cannot be empty.";

            return crow::response(400, response);
        }

        const char* sql =
            "UPDATE candidates SET "
            "name=?, position=?, department=?, manifesto=?, achievements=? "
            "WHERE id=?;";

        sqlite3_stmt* stmt = nullptr;

        if (sqlite3_prepare_v2(
            db, sql, -1, &stmt, nullptr
        ) != SQLITE_OK)
        {
            response["status"] = "error";
            response["message"] = "Database error.";

            return crow::response(500, response);
        }

        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, position.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, department.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, manifesto.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, achievements.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 6, id);

        if (sqlite3_step(stmt) != SQLITE_DONE)
        {
            sqlite3_finalize(stmt);

            response["status"] = "error";
            response["message"] = "Could not update candidate.";

            return crow::response(500, response);
        }

        int changes = sqlite3_changes(db);

        sqlite3_finalize(stmt);

        if (changes == 0)
        {
            response["status"] = "error";
            response["message"] = "Candidate not found.";

            return crow::response(404, response);
        }

        response["status"] = "success";
        response["message"] = "Candidate updated successfully.";

        return crow::response(200, response);
    });

    // =================================================
    // DELETE CANDIDATE
    // =================================================

    CROW_ROUTE(app, "/api/candidates/<int>")
    .methods(crow::HTTPMethod::DELETE)
    ([&](const crow::request& req, int id) {

        crow::json::wvalue response;
        if (!adminFor(req)) { response["status"] = "error"; response["message"] = "Administrator authentication required."; return crow::response(401, response); }

        if (id <= 0)
        {
            response["status"] = "error";
            response["message"] = "Invalid candidate ID.";

            return crow::response(400, response);
        }

        const char* sql =
            "DELETE FROM candidates WHERE id=?;";

        sqlite3_stmt* stmt = nullptr;

        if (sqlite3_prepare_v2(
            db, sql, -1, &stmt, nullptr
        ) != SQLITE_OK)
        {
            response["status"] = "error";
            response["message"] = "Database error.";

            return crow::response(500, response);
        }

        sqlite3_bind_int(stmt, 1, id);

        if (sqlite3_step(stmt) != SQLITE_DONE)
        {
            sqlite3_finalize(stmt);

            response["status"] = "error";
            response["message"] =
                "Could not delete candidate. The candidate may have votes.";

            return crow::response(409, response);
        }

        int changes = sqlite3_changes(db);

        sqlite3_finalize(stmt);

        if (changes == 0)
        {
            response["status"] = "error";
            response["message"] = "Candidate not found.";

            return crow::response(404, response);
        }

        response["status"] = "success";
        response["message"] = "Candidate deleted successfully.";

        return crow::response(200, response);
    });


    // =================================================
    // STUDENT REGISTRATION
    // =================================================
    CROW_ROUTE(app, "/api/register")
    .methods(crow::HTTPMethod::POST)
    ([&](const crow::request& req) {
        crow::json::wvalue out;
        string verifyToken = requestToken(req);
        string email;
        { lock_guard<mutex> lock(sessionMutex); auto it = verifiedEmails.find(verifyToken); if (it != verifiedEmails.end()) { if (chrono::steady_clock::now() <= it->second.second) email = it->second.first; else verifiedEmails.erase(it); } }
        auto body = crow::json::load(req.body);
        if (email.empty()) { out["status"]="error"; out["message"]="Email verification expired. Please verify again."; return crow::response(401,out); }
        if (!body || !body.has("name") || !body.has("scholar_id") || !body.has("department")) { out["status"]="error"; out["message"]="Name, 7-digit scholar ID and department are required."; return crow::response(400,out); }
        string name=body["name"].s(), scholar=body["scholar_id"].s(), dept=body["department"].s();
        if (name.empty() || dept.empty() || scholar.size()!=7 || !all_of(scholar.begin(),scholar.end(),[](unsigned char c){return isdigit(c);})) { out["status"]="error"; out["message"]="Enter a name, department and exactly 7-digit scholar ID."; return crow::response(400,out); }
        sqlite3_stmt* stmt=nullptr;
        const char* sql="INSERT INTO students(name,email,scholar_id,department,email_verified) VALUES(?,?,?,?,1)";
        if (sqlite3_prepare_v2(db,sql,-1,&stmt,nullptr)!=SQLITE_OK) { out["status"]="error"; out["message"]="Database error."; return crow::response(500,out); }
        sqlite3_bind_text(stmt,1,name.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,2,email.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,3,scholar.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,4,dept.c_str(),-1,SQLITE_TRANSIENT);
        int rc=sqlite3_step(stmt); sqlite3_finalize(stmt);
        if(rc!=SQLITE_DONE) { out["status"]="error"; out["message"]="This email or scholar ID is already registered."; return crow::response(409,out); }
        int studentId=(int)sqlite3_last_insert_rowid(db); string token=randomToken();
        { lock_guard<mutex> lock(sessionMutex); verifiedEmails.erase(verifyToken); sessions[token]={studentId,false}; }
        out["status"]="success"; out["message"]="Registration completed."; out["token"]=token; out["student_id"]=studentId; out["name"]=name; out["email"]=email; out["department"]=dept;
        return crow::response(201,out);
    });

    // =================================================
    // ADMIN LOGIN
    // =================================================
    CROW_ROUTE(app, "/api/admin/login")
    .methods(crow::HTTPMethod::POST)
    ([&](const crow::request& req) {
        crow::json::wvalue out; auto body=crow::json::load(req.body);
        if(!body || !body.has("password") || body["password"].s()!=adminPassword) { out["status"]="error"; out["message"]="Incorrect administrator password."; return crow::response(401,out); }
        string token=randomToken(); { lock_guard<mutex> lock(sessionMutex); sessions[token]={0,true}; }
        out["status"]="success"; out["token"]=token; out["message"]="Administrator signed in."; return crow::response(200,out);
    });

    // =================================================
    // ADMIN SESSION CHECK
    // =================================================
    CROW_ROUTE(app, "/api/admin/session")
    ([&](const crow::request& req) {
        crow::json::wvalue out;
        if (!adminFor(req)) {
            out["status"] = "error";
            out["message"] = "Administrator authentication required.";
            return crow::response(401, out);
        }
        out["status"] = "success";
        out["message"] = "Administrator session is active.";
        return crow::response(200, out);
    });

    // =================================================
    // ELECTION STATUS
    // =================================================
    CROW_ROUTE(app, "/api/election")
    ([] {
        crow::json::wvalue out; sqlite3_stmt* stmt=nullptr;
        if(sqlite3_prepare_v2(db,"SELECT id,name,status,results_published FROM elections ORDER BY id DESC LIMIT 1",-1,&stmt,nullptr)!=SQLITE_OK || sqlite3_step(stmt)!=SQLITE_ROW) { sqlite3_finalize(stmt); out["status"]="error"; out["message"]="Election not configured."; return crow::response(500,out); }
        out["status"]="success"; out["id"]=sqlite3_column_int(stmt,0); out["name"]=columnString(stmt,1); out["election_status"]=columnString(stmt,2); out["published"]=sqlite3_column_int(stmt,3)==1; sqlite3_finalize(stmt); return crow::response(200,out);
    });

    CROW_ROUTE(app, "/api/admin/election")
    .methods(crow::HTTPMethod::POST)
    ([&](const crow::request& req) {
        crow::json::wvalue out; if(!adminFor(req)){out["status"]="error";out["message"]="Administrator authentication required.";return crow::response(401,out);}
        auto body=crow::json::load(req.body); if(!body || !body.has("action")){out["status"]="error";out["message"]="Action is required.";return crow::response(400,out);}
        string action=body["action"].s(); int id=activeElectionId(); const char* sql=nullptr;
        if(action=="start") sql="UPDATE elections SET status='ongoing',results_published=0 WHERE id=?";
        else if(action=="stop") sql="UPDATE elections SET status='completed' WHERE id=? AND status='ongoing'";
        else if(action=="publish") sql="UPDATE elections SET results_published=1 WHERE id=? AND status='completed'";
        else {out["status"]="error";out["message"]="Unknown election action.";return crow::response(400,out);}
        sqlite3_stmt* stmt=nullptr; sqlite3_prepare_v2(db,sql,-1,&stmt,nullptr); sqlite3_bind_int(stmt,1,id); int rc=sqlite3_step(stmt); int changed=sqlite3_changes(db); sqlite3_finalize(stmt);
        if(rc!=SQLITE_DONE || !changed){out["status"]="error";out["message"]="Action not allowed in the current election state.";return crow::response(409,out);}
        out["status"]="success";out["message"]="Election updated.";return crow::response(200,out);
    });

    // =================================================
    // STUDENT PROFILE AND VOTES
    // =================================================
    CROW_ROUTE(app, "/api/me")
    ([&](const crow::request& req) {
        crow::json::wvalue out; SessionData session; if(!sessionFor(req,session)||session.admin){out["status"]="error";out["message"]="Student sign-in required.";return crow::response(401,out);}
        sqlite3_stmt* stmt=nullptr; sqlite3_prepare_v2(db,"SELECT name,email,scholar_id,department FROM students WHERE id=?",-1,&stmt,nullptr); sqlite3_bind_int(stmt,1,session.studentId);
        if(sqlite3_step(stmt)!=SQLITE_ROW){sqlite3_finalize(stmt);out["status"]="error";out["message"]="Student not found.";return crow::response(404,out);}
        out["status"]="success";out["name"]=columnString(stmt,0);out["email"]=columnString(stmt,1);out["scholar_id"]=columnString(stmt,2);out["department"]=columnString(stmt,3);sqlite3_finalize(stmt);return crow::response(200,out);
    });

    CROW_ROUTE(app, "/api/my-votes")
    ([&](const crow::request& req) {
        crow::json::wvalue out; SessionData session; if(!sessionFor(req,session)||session.admin){out["status"]="error";out["message"]="Student sign-in required.";return crow::response(401,out);}
        int election=activeElectionId(); sqlite3_stmt* stmt=nullptr; sqlite3_prepare_v2(db,"SELECT position,candidate_id FROM votes WHERE student_id=? AND election_id=?",-1,&stmt,nullptr); sqlite3_bind_int(stmt,1,session.studentId);sqlite3_bind_int(stmt,2,election);
        crow::json::wvalue::object votes; while(sqlite3_step(stmt)==SQLITE_ROW) votes[columnString(stmt,0)]=sqlite3_column_int(stmt,1);sqlite3_finalize(stmt);out["status"]="success";out["votes"]=std::move(votes);return crow::response(200,out);
    });

    // =================================================
    // CAST VOTE (one per student and position enforced by SQLite)
    // =================================================
    CROW_ROUTE(app, "/api/vote")
    .methods(crow::HTTPMethod::POST)
    ([&](const crow::request& req) {
        crow::json::wvalue out; SessionData session; if(!sessionFor(req,session)||session.admin){out["status"]="error";out["message"]="Student sign-in required.";return crow::response(401,out);}
        auto body=crow::json::load(req.body); if(!body||!body.has("candidate_id")){out["status"]="error";out["message"]="Candidate ID is required.";return crow::response(400,out);}
        int candidate=body["candidate_id"].i(), election=activeElectionId(); if(candidate<=0){out["status"]="error";out["message"]="Invalid candidate.";return crow::response(400,out);}
        sqlite3_stmt* check=nullptr; sqlite3_prepare_v2(db,"SELECT c.position,e.status FROM candidates c CROSS JOIN elections e WHERE c.id=? AND e.id=?",-1,&check,nullptr);sqlite3_bind_int(check,1,candidate);sqlite3_bind_int(check,2,election);
        if(sqlite3_step(check)!=SQLITE_ROW){sqlite3_finalize(check);out["status"]="error";out["message"]="Candidate not found.";return crow::response(404,out);} string pos=columnString(check,0), status=columnString(check,1);sqlite3_finalize(check);
        if(status!="ongoing"){out["status"]="error";out["message"]="Voting is not currently open.";return crow::response(409,out);}
        sqlite3_stmt* ins=nullptr;sqlite3_prepare_v2(db,"INSERT INTO votes(student_id,election_id,candidate_id,position) VALUES(?,?,?,?)",-1,&ins,nullptr);sqlite3_bind_int(ins,1,session.studentId);sqlite3_bind_int(ins,2,election);sqlite3_bind_int(ins,3,candidate);sqlite3_bind_text(ins,4,pos.c_str(),-1,SQLITE_TRANSIENT);int rc=sqlite3_step(ins);sqlite3_finalize(ins);
        if(rc!=SQLITE_DONE){out["status"]="error";out["message"]="You have already voted for this position.";return crow::response(409,out);}out["status"]="success";out["message"]="Your vote has been recorded.";return crow::response(201,out);
    });

    // =================================================
    // PUBLISHED RESULTS
    // =================================================
    CROW_ROUTE(app, "/api/results")
    ([] {
        crow::json::wvalue out; int election=activeElectionId(); sqlite3_stmt* gate=nullptr;sqlite3_prepare_v2(db,"SELECT status,results_published FROM elections WHERE id=?",-1,&gate,nullptr);sqlite3_bind_int(gate,1,election);
        bool allowed=sqlite3_step(gate)==SQLITE_ROW && columnString(gate,0)=="completed" && sqlite3_column_int(gate,1)==1;sqlite3_finalize(gate);if(!allowed){out["status"]="error";out["message"]="Results have not been published.";return crow::response(403,out);}
        sqlite3_stmt* stmt=nullptr;sqlite3_prepare_v2(db,"SELECT c.id,c.name,c.position,COUNT(v.id) FROM candidates c LEFT JOIN votes v ON v.candidate_id=c.id AND v.election_id=? GROUP BY c.id ORDER BY c.position,c.id",-1,&stmt,nullptr);sqlite3_bind_int(stmt,1,election);crow::json::wvalue::list rows;
        while(sqlite3_step(stmt)==SQLITE_ROW){crow::json::wvalue row;row["candidate_id"]=sqlite3_column_int(stmt,0);row["name"]=columnString(stmt,1);row["position"]=columnString(stmt,2);row["votes"]=sqlite3_column_int(stmt,3);rows.push_back(std::move(row));}sqlite3_finalize(stmt);out["status"]="success";out["results"]=std::move(rows);return crow::response(200,out);
    });

    // =================================================
    // START SERVER
    // =================================================

    cout << "NITS Voting Backend running at http://localhost:18080"
         << endl;

    app.port(18080).multithreaded().run();

    sqlite3_close(db);
    curl_global_cleanup();

    return 0;
}
