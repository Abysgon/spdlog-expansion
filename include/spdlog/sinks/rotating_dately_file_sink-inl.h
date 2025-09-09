#pragma once

#ifndef SPDLOG_HEADER_ONLY
    #include <spdlog/sinks/rotating_dately_file_sink.h>
#endif

#include <spdlog/common.h>

#include <spdlog/details/file_helper.h>
#include <spdlog/details/null_mutex.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/pattern_formatter.h>

#include <cerrno>
#include <ctime>
#include <mutex>
#include <string>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <sstream>
#include <iomanip>

#ifdef _WIN32
    #include <windows.h>
    #define S_ISDIR(mode) (((mode) & _S_IFMT) == _S_IFDIR)
#else
    #include <sys/stat.h>
    #define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#endif

namespace spdlog {
namespace sinks {

template <typename Mutex>
SPDLOG_INLINE bool rotating_dately_file_sink<Mutex>::create_directories(const filename_t &path) {
    if (path.empty()) return true;

#ifdef _WIN32
    /* Windows系统处理逻辑 */
    /* 将窄字符路径转换为宽字符路径 */
    std::wstring wide_path;
    size_t required_size = mbstowcs(NULL, path.c_str(), 0);
    wide_path.resize(required_size);
    mbstowcs(&wide_path[0], path.c_str(), required_size);

    /* 使用宽字符API */
    DWORD attributes = GetFileAttributesW(wide_path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        /* 检查是否为目录 */
        if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
            return true;
        }
        /* 存在但不是目录，返回错误 */
        return false;
    }

    /* 处理路径中的正斜杠（统一为反斜杠） */
    std::string path_str = path;
    std::replace(path_str.begin(), path_str.end(), '/', '\\');

    /* 查找最后一个路径分隔符 */
    size_t pos = path_str.find_last_of('\\');
    if (pos != std::string::npos) {
        /* 递归创建父目录 */
        filename_t parent_dir = path_str.substr(0, pos);
        if (!create_directories(parent_dir)) {
            return false;
        }
    }

    /* 创建当前目录（使用Windows API） */
    return CreateDirectoryW(wide_path.c_str(), NULL) != 0;

#else
    /* Unix系统处理逻辑 */
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return true;
        } else {
            /* 存在但不是目录，返回错误 */
            return false;
        }
    }

    /* 处理错误码：如果是ENOENT（没有那个文件或目录），则创建父目录 */
    if (errno != ENOENT) {
        return false;
    }

    /* 查找最后一个路径分隔符（仅处理正斜杠） */
    size_t pos = path.find_last_of('/');
    if (pos != filename_t::npos) {
        /* 递归创建父目录 */
        filename_t parent_dir = path.substr(0, pos);
        if (!create_directories(parent_dir)) {
            return false;
        }
    }

    /* 创建当前目录（使用Unix API） */
    return mkdir(path.c_str(), 0777) == 0;
#endif
}

/* 辅助函数：提取目录部分 */
template <typename Mutex>
SPDLOG_INLINE filename_t
rotating_dately_file_sink<Mutex>::extract_directory(const filename_t &path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == filename_t::npos) {
        return ""; /* 当前目录 */
    }
    /* 处理末尾分隔符（如 "/path/to/dir/"） */
    if (pos == path.size() - 1) {
        return extract_directory(path.substr(0, pos));
    }
    return path.substr(0, pos);
}

/* 辅助函数：提取文件名部分 */
template <typename Mutex>
SPDLOG_INLINE filename_t
rotating_dately_file_sink<Mutex>::extract_filename(const filename_t &path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == filename_t::npos) {
        return path;
    }
    /* 处理末尾分隔符（如 "/path/to/dir/"） */
    if (pos == path.size() - 1) {
        return "";
    }
    return path.substr(pos + 1);
}

/* 辅助函数：检查文件是否存在（区分文件和目录） */
template <typename Mutex>
SPDLOG_INLINE bool rotating_dately_file_sink<Mutex>::file_exists(const filename_t &path) {
#ifdef _WIN32
    /* Windows系统：使用GetFileAttributes检查文件存在性 */
    /* 将窄字符路径转换为宽字符路径 */
    std::wstring wide_path;
    size_t required_size = mbstowcs(NULL, path.c_str(), 0);
    wide_path.resize(required_size);
    mbstowcs(&wide_path[0], path.c_str(), required_size);

    /* 使用宽字符API */
    DWORD fileAttributes = GetFileAttributesW(wide_path.c_str());
    if (fileAttributes == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    /* 排除目录类型 */
    return !(fileAttributes & FILE_ATTRIBUTE_DIRECTORY);
#else
    /* Unix系统：使用stat检查文件存在性并验证是否为普通文件 */
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }
    return S_ISREG(st.st_mode);
#endif
}

/* 辅助函数：重命名文件 */
template <typename Mutex>
SPDLOG_INLINE bool rotating_dately_file_sink<Mutex>::rename_file(const filename_t &src,
                                                                 const filename_t &dst) {
    return (rename(src.c_str(), dst.c_str()) == 0);
}

/* 辅助函数：获取文件修改时间 */
template <typename Mutex>
SPDLOG_INLINE time_t
rotating_dately_file_sink<Mutex>::get_file_modification_time(const filename_t &path) {
#ifdef _WIN32
    /* Windows系统实现 */
    FILETIME fileTime;
    HANDLE hFile = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hFile == INVALID_HANDLE_VALUE) {
        return 0;
    }

    if (!GetFileTime(hFile, NULL, NULL, &fileTime)) {
        CloseHandle(hFile);
        return 0;
    }

    CloseHandle(hFile);

    /* 将FILETIME转换为time_t */
    SYSTEMTIME systemTime;
    FileTimeToSystemTime(&fileTime, &systemTime);

    tm tmTime = {};
    tmTime.tm_year = systemTime.wYear - 1900;
    tmTime.tm_mon = systemTime.wMonth - 1;
    tmTime.tm_mday = systemTime.wDay;
    tmTime.tm_hour = systemTime.wHour;
    tmTime.tm_min = systemTime.wMinute;
    tmTime.tm_sec = systemTime.wSecond;

    return mktime(&tmTime);
#else
    /* Unix系统实现 */
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return 0;
    }
    return st.st_mtime;
#endif
}

/* 辅助函数：从文件名中提取时间 */
template <typename Mutex>
SPDLOG_INLINE std::time_t rotating_dately_file_sink<Mutex>::extract_time_from_filename(
    const filename_t &filename) {
    /* 假设文件名格式为 "app_YYYYmmdd_HHMMSS.log" */
    size_t start_pos = filename.find("app_") + 4;
    size_t end_pos = filename.find(".log");
    std::string time_str = filename.substr(start_pos, end_pos - start_pos);

    std::tm tm = {};
    std::istringstream ss(time_str);
    ss >> std::get_time(&tm, "%Y%m%d_%H%M%S");
    return std::mktime(&tm);
}

template <typename Mutex>
SPDLOG_INLINE rotating_dately_file_sink<Mutex>::rotating_dately_file_sink(
    const filename_t &base_filename,
    std::chrono::hours max_age,
    std::size_t max_size,
    std::size_t max_files,
    bool truncate,
    const file_event_handlers &event_handlers)
    : base_filename_(std::move(base_filename)),
      max_age_(max_age),
      max_size_(max_size),
      max_files_(max_files),
      truncate_(truncate),
      file_helper_{event_handlers},
      filenames_q_(),
      current_size_(0) {
    if (max_size == 0) {
        throw_spdlog_ex("rotating_dately_file_sink_new constructor: max_size arg cannot be zero");
    }

    if (max_files > MaxFiles) {
        throw_spdlog_ex(
            "rotating_dately_file_sink_new constructor: max_files arg cannot exceed MaxFiles");
    }

    /* 分离目录和文件名 */
    directory_ = extract_directory(base_filename_);
    base_filename_only_ = extract_filename(base_filename_);

    /* 确保目录存在 */
    if (!directory_.empty()) {
        create_directories(directory_);
    }

    /* 打开当前日志文件（使用原始文件名） */
    file_helper_.open(base_filename_, truncate_);
    rotation_tp_ = next_rotation_tp_();
    current_size_ = file_helper_.size();

    if (max_files_ > 0) {
        init_filenames_q_();
    }

    /* 手动调用一次清理函数,防止软件在不会持续运行到第二天时一直不执行清理 */
    clean_old_files();
}

template <typename Mutex>
SPDLOG_INLINE void rotating_dately_file_sink<Mutex>::set_max_date(std::chrono::hours max_age) {
    std::lock_guard<Mutex> lock(base_sink<Mutex>::mutex_);
    if (max_age < std::chrono::hours(24)) {
        throw_spdlog_ex(
            "rotating_dately_file_sink_new set_max_age: max_age arg cannot be lesser than 1 Day");
    }
    max_age_ = max_age;
    clean_old_files();
}

template <typename Mutex>
SPDLOG_INLINE void rotating_dately_file_sink<Mutex>::set_max_size(std::size_t max_size) {
    std::lock_guard<Mutex> lock(base_sink<Mutex>::mutex_);
    if (max_size == 0) {
        throw_spdlog_ex("rotating_dately_file_sink_new set_max_size: max_size arg cannot be zero");
    }
    max_size_ = max_size;
}

template <typename Mutex>
SPDLOG_INLINE void rotating_dately_file_sink<Mutex>::set_max_files(std::size_t max_files) {
    std::lock_guard<Mutex> lock(base_sink<Mutex>::mutex_);
    if (max_files > MaxFiles) {
        throw_spdlog_ex(
            "rotating_dately_file_sink_new set_max_files: max_files arg cannot exceed 200000");
    }
    max_files_ = max_files;
    if (max_files_ > 0) {
        init_filenames_q_();
    }
    clean_old_files();
}

/* 设置日志格式 */
template <typename Mutex>
SPDLOG_INLINE void rotating_dately_file_sink<Mutex>::set_dately_file_pattern(
    const std::string &pattern) {
    std::lock_guard<Mutex> lock(base_sink<Mutex>::mutex_);
    base_sink<Mutex>::formatter_ =
        std::unique_ptr<spdlog::formatter>(new spdlog::pattern_formatter(pattern));
}

/* 修改当前日志文件名 */
template <typename Mutex>
SPDLOG_INLINE void rotating_dately_file_sink<Mutex>::set_current_filename(
    const filename_t &new_filename) {
    std::lock_guard<Mutex> lock(base_sink<Mutex>::mutex_);

    /* 关闭当前文件 */
    file_helper_.close();

    /* 构建新的完整路径 */
    filename_t new_full_path = directory_;
    if (!new_full_path.empty() && new_full_path.back() != '/' && new_full_path.back() != '\\') {
        new_full_path += '/';
    }
    new_full_path += new_filename;

    /* 重命名当前文件 */
    if (file_exists(base_filename_)) {
        if (!rename_file(base_filename_, new_full_path)) {
            /* 重命名失败，尝试再次打开原文件 */
            file_helper_.open(base_filename_, truncate_);
            throw_spdlog_ex("rotating_dately_file_sink_new: failed renaming " +
                                details::os::filename_to_str(base_filename_) + " to " +
                                details::os::filename_to_str(new_full_path),
                            errno);
        }
    }

    /* 更新文件名 */
    base_filename_ = new_full_path;
    base_filename_only_ = new_filename;

    /* 打开新文件 */
    file_helper_.open(base_filename_, truncate_);
    current_size_ = file_helper_.size();
}

template <typename Mutex>
SPDLOG_INLINE filename_t rotating_dately_file_sink<Mutex>::filename() {
    std::lock_guard<Mutex> lock(base_sink<Mutex>::mutex_);
    return file_helper_.filename();
}

template <typename Mutex>
SPDLOG_INLINE void rotating_dately_file_sink<Mutex>::sink_it_(const details::log_msg &msg) {
    auto time = msg.time;
    bool should_rotate = time >= rotation_tp_;
    memory_buf_t formatted;
    base_sink<Mutex>::formatter_->format(msg, formatted);
    auto new_size = current_size_ + formatted.size();

    if (new_size > max_size_ || should_rotate) {
        rotate_();
        new_size = formatted.size();
    }

    file_helper_.write(formatted);
    current_size_ = new_size;

    if (should_rotate) {
        rotation_tp_ = next_rotation_tp_();
        clean_old_files();
    }
}

template <typename Mutex>
SPDLOG_INLINE void rotating_dately_file_sink<Mutex>::flush_() {
    file_helper_.flush();
}

template <typename Mutex>
SPDLOG_INLINE tm rotating_dately_file_sink<Mutex>::now_tm(log_clock::time_point tp) {
    time_t tnow = log_clock::to_time_t(tp);
    return spdlog::details::os::localtime(tnow);
}

template <typename Mutex>
SPDLOG_INLINE log_clock::time_point rotating_dately_file_sink<Mutex>::next_rotation_tp_() {
    auto now = log_clock::now();
    tm date = now_tm(now);
    date.tm_hour = 0;
    date.tm_min = 0;
    date.tm_sec = 0;
    auto rotation_time = log_clock::from_time_t(std::mktime(&date));
    if (rotation_time > now) {
        return rotation_time;
    }
    return {rotation_time + std::chrono::hours(24)};
}

/* 计算备份文件名 */
template <typename Mutex>
SPDLOG_INLINE filename_t rotating_dately_file_sink<Mutex>::calc_backup_filename(const tm &tm_info) {
    filename_t full_dir = directory_;
    if (!full_dir.empty() && full_dir.back() != '/' && full_dir.back() != '\\') {
        full_dir += '/';
    }

    /* 预期格式: "app_YYYYmmdd_HHMMSS.log" */
    return fmt_lib::format(SPDLOG_FILENAME_T("{}app_{:04d}{:02d}{:02d}_{:02d}{:02d}{:02d}.log"),
                           full_dir, tm_info.tm_year + 1900, tm_info.tm_mon + 1, tm_info.tm_mday,
                           tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec);
}

/* 文件旋转逻辑 */
template <typename Mutex>
SPDLOG_INLINE void rotating_dately_file_sink<Mutex>::rotate_() {
    using details::os::filename_to_str;

    /* 关闭当前文件 */
    file_helper_.close();

    /* 获取当前时间，用于生成备份文件名 */
    auto now = log_clock::now();
    tm now_tm_info = now_tm(now);
    filename_t backup_filename = calc_backup_filename(now_tm_info);

    /* 重命名当前文件为备份文件 */
    if (file_exists(base_filename_)) {
        if (!rename_file(base_filename_, backup_filename)) {
            /* 重命名失败，尝试再次打开原文件继续写入 */
            file_helper_.open(base_filename_, truncate_);
            current_size_ = file_helper_.size();
            throw_spdlog_ex("rotating_dately_file_sink_new: failed renaming " +
                                filename_to_str(base_filename_) + " to " +
                                filename_to_str(backup_filename),
                            errno);
        }
    }

    /* 打开新的日志文件（使用原始文件名） */
    file_helper_.open(base_filename_, truncate_);
    current_size_ = 0;

    /* 将新的备份文件添加到队列 */
    if (max_files_ > 0) {
        if (filenames_q_.full()) {
            filenames_q_.pop_front();
        }
        /* 使用拷贝而非移动语义，避免rvalue引用错误 */
        filenames_q_.push_back(std::move(backup_filename));
    }
}

/* 初始化文件名队列 */
template <typename Mutex>
SPDLOG_INLINE void rotating_dately_file_sink<Mutex>::init_filenames_q_() {
    filenames_q_ = details::circular_q<filename_t>(static_cast<size_t>(max_files_));

    if (directory_.empty()) {
        return;
    }

    /* 使用平台兼容的目录遍历方法 */
    std::vector<filename_t> backup_files;

#ifdef _WIN32
    /* Windows平台 */
    WIN32_FIND_DATAA find_data;
    HANDLE hFind = FindFirstFileA((directory_ + "\\app_*.log").c_str(), &find_data);

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                backup_files.push_back(directory_ + "\\" + find_data.cFileName);
            }
        } while (FindNextFileA(hFind, &find_data) != 0);

        FindClose(hFind);
    }
#else
    /* POSIX平台 */
    DIR *dir = opendir(directory_.c_str());
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (strncmp(entry->d_name, "app_", 4) == 0 &&
                strstr(entry->d_name, ".log") != nullptr) {
                backup_files.push_back(directory_ + "/" + entry->d_name);
            }
        }
        closedir(dir);
    }
#endif

    /* 按时间排序（最新的文件在前） */
    std::sort(backup_files.begin(), backup_files.end(),
              [this](const filename_t &a, const filename_t &b) {
                  return extract_time_from_filename(a) > extract_time_from_filename(b);
              });

    /* 添加到队列（保留最新的max_files_个文件） */
    for (const auto &file : backup_files) {
        if (filenames_q_.size() >= max_files_) {
            break;
        }
        std::basic_string<char> theFile = file;
        filenames_q_.push_back(std::move(theFile));
    }
}

/* 清理旧文件 */
template <typename Mutex>
SPDLOG_INLINE void rotating_dately_file_sink<Mutex>::clean_old_files() {
    if (directory_.empty()) {
        return;
    }

    /* 收集所有备份文件 */
    std::vector<filename_t> backup_files;

#ifdef _WIN32
    /* Windows平台 */
    WIN32_FIND_DATAA find_data;
    HANDLE hFind = FindFirstFileA((directory_ + "\\app_*.log").c_str(), &find_data);

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                backup_files.push_back(directory_ + "\\" + find_data.cFileName);
            }
        } while (FindNextFileA(hFind, &find_data) != 0);

        FindClose(hFind);
    }
#else
    /* POSIX平台 */
    DIR *dir = opendir(directory_.c_str());
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (strncmp(entry->d_name, "app_", 4) == 0 &&
                strstr(entry->d_name, ".log") != nullptr) {
                backup_files.push_back(directory_ + "/" + entry->d_name);
            }
        }
        closedir(dir);
    }
#endif

    /* 根据修改时间排序（最旧的文件在前） */
    std::sort(backup_files.begin(), backup_files.end(),
              [this](const filename_t &a, const filename_t &b) {
                  return extract_time_from_filename(a) < extract_time_from_filename(b);
              });

    /* 删除超出数量限制的旧文件 */
    if (max_files_ != 0) {
        if (backup_files.size() > max_files_) {
            size_t files_to_delete = backup_files.size() - max_files_;
            for (size_t i = 0; i < files_to_delete; ++i) {
                remove(backup_files[i].c_str());
            }
        }
    }

    /* 删除超过保留时间的旧文件 */
    auto now = std::time(nullptr);
    auto max_age_seconds = std::chrono::duration_cast<std::chrono::seconds>(max_age_).count();
    for (const auto &file : backup_files) {
        auto file_time = extract_time_from_filename(file);
        if (now - file_time > max_age_seconds) {
            remove(file.c_str());
        }
    }
}

}  // namespace sinks
}  // namespace spdlog
