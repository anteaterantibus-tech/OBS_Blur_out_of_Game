// OBS Privacy Guard - сканирование папки с играми.
// Только чтение каталога, ищет *.exe на небольшой глубине,
// служебные установщики и краш-репортеры отсеиваются.
// Без реестра, без Steam API: работает с любой папкой (Steam, Epic, GOG,
// отдельные игры). Не создает окон, не меняет фокус.
#pragma once

#include <string>
#include <vector>

namespace privacy_guard {

// Найти EXE игр в каталоге root (только имена файлов, без путей).
// max_depth: глубина подкаталогов (0 = только сам каталог).
// Возвращает отсортированный список без дубликатов (макс. 500).
std::vector<std::string> scan_games_folder(const std::string &root, int max_depth = 3);

} // namespace privacy_guard
