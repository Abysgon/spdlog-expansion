#pragma once

#include "spdlog/sinks/base_sink.h"
#include "spdlog/details/file_helper.h"
#include "spdlog/details/circular_q.h"
#include <chrono>
#include <mutex>
#include <ctime>

/* 平台兼容性头文件 */
#ifdef _WIN32
    #include <direct.h>
    #include <io.h>
#else
    #include <sys/stat.h>
    #include <unistd.h>
    #include <dirent.h>
#endif

namespace spdlog {
namespace sinks {

template <typename Mutex>
class rotating_dately_file_sink final : public base_sink<Mutex> {
public:
    explicit rotating_dately_file_sink(const filename_t &base_filename,
                                       std::chrono::hours max_age = std::chrono::hours(24 * 30),
                                       std::size_t max_size = 1024 * 1024 *
                                                              10, /* 默认最大文件大小 10MB */
                                       std::size_t max_files = 0,
                                       bool truncate = false,
                                       const file_event_handlers &event_handlers = {});

    void set_max_date(std::chrono::hours max_age);
    void set_max_size(std::size_t max_size);
    void set_max_files(std::size_t max_files);
    void set_dately_file_pattern(const std::string &pattern);  /* 设置日志格式 */
    void set_current_filename(const filename_t &new_filename); /* 修改当前日志文件名 */

    filename_t filename();

protected:
    void sink_it_(const details::log_msg &msg) override;
    void flush_() override;

private:
    static constexpr size_t MaxFiles = 200000;

    tm now_tm(log_clock::time_point tp);
    log_clock::time_point next_rotation_tp_();
    filename_t calc_backup_filename(const tm &tm_info);
    void init_filenames_q_();
    void clean_old_files();
    void rotate_();

    /* 辅助函数 */
    bool create_directories(const filename_t &path);
    filename_t extract_directory(const filename_t &path);
    filename_t extract_filename(const filename_t &path);
    bool file_exists(const filename_t &path);
    bool rename_file(const filename_t &src, const filename_t &dst);
    time_t get_file_modification_time(const filename_t &path);
    std::time_t extract_time_from_filename(const std::string &filename);

    filename_t base_filename_;      /* 完整路径和原始文件名 */
    filename_t base_filename_only_; /* 仅包含文件名部分 */
    filename_t directory_;          /* 仅包含目录部分 */
    log_clock::time_point rotation_tp_;
    details::file_helper file_helper_;
    std::chrono::hours max_age_;
    std::size_t max_size_;
    std::size_t max_files_;
    bool truncate_;
    details::circular_q<filename_t> filenames_q_;
    std::size_t current_size_;
};

using rotating_dately_file_sink_mt = rotating_dately_file_sink<std::mutex>;
using rotating_dately_file_sink_st = rotating_dately_file_sink<details::null_mutex>;

}  // namespace sinks
}  // namespace spdlog

/* 包含内联实现 */
#ifdef SPDLOG_HEADER_ONLY
    #include "rotating_dately_file_sink-inl.h"
#endif
