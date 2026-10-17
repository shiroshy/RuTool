#pragma once
#include <QString>
void cleanupAbandonedUpdates(const QString& dataRoot);
bool isTranslationDownload(const QString& dataRoot, const QString& path);
void cleanupTranslationDownloads(const QString& dataRoot, const QString& preservedPath = {});
