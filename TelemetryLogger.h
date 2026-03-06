#pragma once

#include <string>
#include <vector>
#include "iracing.h"

class TelemetryLogger
{
public:
    void update(ConnectionStatus status);

private:
    std::vector<std::string> readCurrentValues() const;
    void appendCurrentRow(const std::vector<std::string>& values);
    void flushLapBuffer();

    static std::string buildHeader();
    static std::string buildRow(const std::vector<std::string>& values);
    static std::string buildFileStem();
    static std::string buildFullPath(const std::string& stem);
    static std::string buildDatePrefix();
    static std::string sanitizeFilePart(const std::string& value);
    static bool fileHasContent(const std::string& filePath);
    static void appendLinesToFile(const std::string& filePath, const std::string& header, const std::vector<std::string>& lines);

private:
    int m_currentLap = -1;
    bool m_haveLastValues = false;
    std::vector<std::string> m_lastValues;
    std::vector<std::string> m_lapRows;
    std::string m_fileStem;
};
