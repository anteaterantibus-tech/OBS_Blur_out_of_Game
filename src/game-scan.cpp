// OBS Privacy Guard - сканирование папки с играми.
#include "game-scan.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace privacy_guard {
namespace fs = std::filesystem;
namespace {

std::string lower_ascii(const std::string &s)
{
	std::string out = s;
	for (char &c : out)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return out;
}

bool has_exe_ext(const fs::path &p)
{
	std::string ext = lower_ascii(p.extension().u8string());
	return ext == ".exe";
}

// Служебные EXE, которые игрой не являются.
bool is_helper_exe(const std::string &lower_name)
{
	static const char *const markers[] = {"uninstall", "unins",      "setup",   "installer",
					      "crash",     "vcredist",   "dxsetup", "dotnet",
					      "oalinst",   "redist",     "report",  "helper",
					      "launcher-manager"};
	for (const char *m : markers) {
		if (lower_name.find(m) != std::string::npos)
			return true;
	}
	return false;
}

void scan_dir(const fs::path &dir, int depth_left, std::vector<std::string> &out)
{
	if (out.size() >= 500)
		return;
	std::error_code ec;
	fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
	if (ec)
		return;
	for (const auto &entry : it) {
		if (out.size() >= 500)
			return;
		std::error_code ec2;
		if (entry.is_symlink(ec2))
			continue; // ссылки не раскрываем: защита от петель
		if (entry.is_regular_file(ec2)) {
			const fs::path &p = entry.path();
			if (!has_exe_ext(p))
				continue;
			std::string name = p.filename().u8string();
			if (is_helper_exe(lower_ascii(name)))
				continue;
			std::string low = lower_ascii(name);
			bool dup = false;
			for (const std::string &e : out) {
				if (lower_ascii(e) == low) {
					dup = true;
					break;
				}
			}
			if (!dup)
				out.push_back(name);
		} else if (entry.is_directory(ec2) && depth_left > 0) {
			scan_dir(entry.path(), depth_left - 1, out);
		}
	}
}

} // namespace

std::vector<std::string> scan_games_folder(const std::string &root, int max_depth)
{
	std::vector<std::string> out;
	if (root.empty() || max_depth < 0)
		return out;
	fs::path base(root);
	std::error_code ec;
	if (!fs::is_directory(base, ec))
		return out;
	scan_dir(base, max_depth, out);
	std::sort(out.begin(), out.end(), [](const std::string &a, const std::string &b) {
		return lower_ascii(a) < lower_ascii(b);
	});
	return out;
}

} // namespace privacy_guard
