#include "manifest.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>

#include <opencv2/videoio.hpp>

#include "naming.hpp"

#include <openssl/evp.h>

std::string stateToString(UploadState state) {
    switch (state) {
        case UploadState::Local: return "local";
        case UploadState::UploadPending: return "upload_pending";
        case UploadState::OffloadConfirmed: return "offload_confirmed";
    }
    return "local";
}

UploadState stateFromString(const std::string& s) {
    if (s == "upload_pending") return UploadState::UploadPending;
    if (s == "offload_confirmed") return UploadState::OffloadConfirmed;
    return UploadState::Local;
}

namespace {

std::string jsonEscape(const std::string& in) {
    std::string out;
    for (char c : in) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            std::ostringstream oss;
            oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c;
            out += oss.str();
        } else {
            out += c;
        }
    }
    return out;
}

// Minimal extractor for our own fixed schema: value of "key": "value".
bool extractString(const std::string& line, const std::string& key, std::string& out) {
    std::string needle = "\"" + key + "\": \"";
    auto pos = line.find(needle);
    if (pos == std::string::npos) {
        return false;
    }
    pos += needle.size();
    auto end = line.find('"', pos);
    if (end == std::string::npos) {
        return false;
    }
    // Unescape.
    std::string raw = line.substr(pos, end - pos);
    out.clear();
    for (std::size_t i = 0; i < raw.size(); i++) {
        if (raw[i] == '\\' && i + 1 < raw.size()) {
            i++;
            out += raw[i];
        } else {
            out += raw[i];
        }
    }
    return true;
}

bool extractNumber(const std::string& line, const std::string& key, double& out) {
    std::string needle = "\"" + key + "\": ";
    auto pos = line.find(needle);
    if (pos == std::string::npos) {
        return false;
    }
    pos += needle.size();
    try {
        out = std::stod(line.substr(pos));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::string nowStamp() {
    return naming::timestamp(std::chrono::system_clock::now());
}

} // namespace

std::string sha256File(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return "";
    }
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        return "";
    }
    std::string result;
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1) {
        std::vector<char> buf(65536);
        bool ok = true;
        while (file) {
            file.read(buf.data(), buf.size());
            std::streamsize n = file.gcount();
            if (n > 0 && EVP_DigestUpdate(ctx, buf.data(), n) != 1) {
                ok = false;
                break;
            }
        }
        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned int len = 0;
        if (ok && EVP_DigestFinal_ex(ctx, digest, &len) == 1) {
            std::ostringstream oss;
            for (unsigned int i = 0; i < len; i++) {
                oss << std::hex << std::setw(2) << std::setfill('0') << (int)digest[i];
            }
            result = oss.str();
        }
    }
    EVP_MD_CTX_free(ctx);
    return result;
}

Manifest::Manifest(std::filesystem::path dir) : path(dir / "manifest.jsonl") {
}

void Manifest::addRecording(const std::filesystem::path& videoDirFile,
                            const std::string& context,
                            const std::string& startedAt,
                            double durationSeconds) {
    ManifestEntry e;
    e.file = videoDirFile.filename().string();
    e.context = context;
    e.startedAt = startedAt;
    e.durationSeconds = durationSeconds;
    std::error_code ec;
    e.sizeBytes = std::filesystem::file_size(videoDirFile, ec);
    if (ec) {
        e.sizeBytes = 0;
    }
    e.sha256 = sha256File(videoDirFile);
    e.state = UploadState::Local;
    e.updated = nowStamp();
    records[e.file] = std::move(e);
}

void Manifest::setState(const std::string& file, UploadState state) {
    auto it = records.find(file);
    if (it != records.end()) {
        it->second.state = state;
        it->second.updated = nowStamp();
    }
}

bool Manifest::contains(const std::string& file) const {
    return records.count(file) > 0;
}

bool Manifest::remove(const std::string& file) {
    return records.erase(file) > 0;
}

std::vector<ManifestEntry> Manifest::entries() const {
    std::vector<ManifestEntry> out;
    out.reserve(records.size());
    for (const auto& kv : records) {
        out.push_back(kv.second);
    }
    std::sort(out.begin(), out.end(), [](const ManifestEntry& a, const ManifestEntry& b) {
        return a.startedAt == b.startedAt ? a.file < b.file : a.startedAt < b.startedAt;
    });
    return out;
}

const ManifestEntry* Manifest::find(const std::string& file) const {
    auto it = records.find(file);
    return it == records.end() ? nullptr : &it->second;
}

bool Manifest::load() {
    records.clear();
    std::ifstream file(path);
    if (!file) {
        return false;
    }
    std::string line;
    int lineNo = 0;
    while (std::getline(file, line)) {
        lineNo++;
        if (line.empty()) {
            continue;
        }
        ManifestEntry e;
        if (!extractString(line, "file", e.file) || !extractString(line, "sha256", e.sha256)) {
            std::cerr << "Manifest: line " << lineNo << " unreadable (" << line << "), skipping" << std::endl;
            continue;
        }
        std::string s;
        double d;
        if (extractString(line, "context", s)) e.context = s;
        if (extractString(line, "started_at", s)) e.startedAt = s;
        if (extractString(line, "state", s)) e.state = stateFromString(s);
        if (extractString(line, "updated", s)) e.updated = s;
        if (extractNumber(line, "duration", d)) e.durationSeconds = d;
        if (extractNumber(line, "size", d)) e.sizeBytes = static_cast<std::uintmax_t>(d);
        records[e.file] = std::move(e);
    }
    return true;
}

bool Manifest::save() const {
    std::filesystem::create_directories(path.parent_path());
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) {
            return false;
        }
        for (const auto& kv : records) {
            const ManifestEntry& e = kv.second;
            out << "{\"file\": \"" << jsonEscape(e.file)
                << "\", \"context\": \"" << jsonEscape(e.context)
                << "\", \"started_at\": \"" << jsonEscape(e.startedAt)
                << "\", \"duration\": " << e.durationSeconds
                << ", \"size\": " << e.sizeBytes
                << ", \"sha256\": \"" << e.sha256
                << "\", \"state\": \"" << stateToString(e.state)
                << "\", \"updated\": \"" << e.updated << "\"}\n";
        }
        out.flush();
        if (!out) {
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec); // Atomic on POSIX.
    return !ec;
}

std::size_t Manifest::rebuildFromDisk() {
    std::size_t added = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(path.parent_path(), ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::string name = entry.path().filename().string();
        if (!naming::matches(name) || contains(name)) {
            continue;
        }
        // dashcam_<context>_<ts>_<seq>.mp4
        std::vector<std::string> parts;
        std::istringstream iss(name.substr(0, name.size() - 4));
        std::string token;
        while (std::getline(iss, token, '_')) {
            parts.push_back(token);
        }
        std::string context = parts.size() > 1 ? parts[1] : "seg";
        std::string startedAt = parts.size() > 3 ? parts[2] + "-" + parts[3] : "";
        addRecording(entry.path(), context, startedAt, 0);
        added++;
    }
    if (added > 0) {
        save();
    }
    return added;
}
