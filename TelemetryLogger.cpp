#include "TelemetryLogger.h"

#include <cstdio>
#include <ctime>
#include "Config.h"

static std::string formatFloat(float value)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6f", value);
    return std::string(buf);
}

static std::string formatInt(int value)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", value);
    return std::string(buf);
}

struct LoggedField
{
    const char* name;
    std::string (*readValue)();
};

static const std::vector<LoggedField> kLoggedFields = {
    {"ir_Lap", []() -> std::string { return formatInt(ir_Lap.getInt()); }},
    {"ir_LapCurrentLapTime", []() -> std::string { return formatFloat(ir_LapCurrentLapTime.getFloat()); }},
    {"ir_Throttle", []() -> std::string { return formatFloat(ir_Throttle.getFloat()); }},
    {"ir_Brake", []() -> std::string { return formatFloat(ir_Brake.getFloat()); }},
    {"ir_SteeringWheelAngle", []() -> std::string { return formatFloat(ir_SteeringWheelAngle.getFloat()); }},
    {"ir_Gear", []() -> std::string { return formatInt(ir_Gear.getInt()); }},
    {"ir_Speed", []() -> std::string { return formatFloat(ir_Speed.getFloat()); }},
    {"ir_Yaw", []() -> std::string { return formatFloat(ir_Yaw.getFloat()); }}
};

void TelemetryLogger::update(ConnectionStatus status)
{
    if (!g_cfg.getBool("TelemetryLogger", "enabled", true))
    {
        flushLapBuffer();
        m_currentLap = -1;
        m_haveLastValues = false;
        return;
    }

    if (status != ConnectionStatus::DRIVING)
    {
        flushLapBuffer();
        m_currentLap = -1;
        m_haveLastValues = false;
        return;
    }

    const std::string newFileStem = buildFileStem();
    if (m_fileStem != newFileStem)
    {
        flushLapBuffer();
        m_fileStem = newFileStem;
        m_currentLap = -1;
        m_haveLastValues = false;
    }

    const int lap = ir_Lap.getInt();
    if (m_currentLap < 0)
        m_currentLap = lap;

    if (lap != m_currentLap)
    {
        flushLapBuffer();
        m_currentLap = lap;
        m_haveLastValues = false;
    }

    const std::vector<std::string> values = readCurrentValues();
    if (!m_haveLastValues || values != m_lastValues)
    {
        appendCurrentRow(values);
        m_lastValues = values;
        m_haveLastValues = true;
    }
}

std::vector<std::string> TelemetryLogger::readCurrentValues() const
{
    std::vector<std::string> values;
    values.reserve(kLoggedFields.size());
    for (const LoggedField& field : kLoggedFields)
        values.push_back(field.readValue());
    return values;
}

void TelemetryLogger::appendCurrentRow(const std::vector<std::string>& values)
{
    m_lapRows.push_back(buildRow(values));
}

void TelemetryLogger::flushLapBuffer()
{
    if (m_lapRows.empty())
        return;

    const std::string stem = m_fileStem.empty() ? buildFileStem() : m_fileStem;
    const std::string fullPath = buildFullPath(stem);
    const std::string header = buildHeader();

    appendLinesToFile(fullPath, header, m_lapRows);
    m_lapRows.clear();
}

std::string TelemetryLogger::buildHeader()
{
    std::string line;
    for (size_t i = 0; i < kLoggedFields.size(); ++i)
    {
        if (i > 0)
            line += ",";
        line += kLoggedFields[i].name;
    }
    return line;
}

std::string TelemetryLogger::buildRow(const std::vector<std::string>& values)
{
    std::string line;
    for (size_t i = 0; i < values.size(); ++i)
    {
        if (i > 0)
            line += ",";
        line += values[i];
    }
    return line;
}

std::string TelemetryLogger::buildFileStem()
{
    const std::string trackName = sanitizeFilePart(ir_session.trackName.empty() ? "unknownTrack" : ir_session.trackName);
    const std::string carName = sanitizeFilePart(ir_session.carName.empty() ? "unknownCar" : ir_session.carName);
    return buildDatePrefix() + "_" + trackName + "_" + carName;
}

std::string TelemetryLogger::buildFullPath(const std::string& stem)
{
    std::string savePath = g_cfg.getString("TelemetryLogger", "save_path", "");

    if (!savePath.empty())
    {
        if (savePath.back() != '\\' && savePath.back() != '/')
            savePath += "\\";
        return savePath + stem + ".csv";
    }

    return stem + ".csv";
}

std::string TelemetryLogger::buildDatePrefix()
{
    std::time_t now = std::time(nullptr);
    std::tm localTm = {};
    localtime_s(&localTm, &now);

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", localTm.tm_year + 1900, localTm.tm_mon + 1, localTm.tm_mday);
    return std::string(buf);
}

std::string TelemetryLogger::sanitizeFilePart(const std::string& value)
{
    std::string out = value;

    for (char& c : out)
    {
        const bool invalid = (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*');
        if (invalid || c == ' ')
            c = '_';
    }

    if (out.empty())
        return "unknown";

    while (!out.empty() && out.back() == '_')
        out.pop_back();

    return out.empty() ? std::string("unknown") : out;
}

bool TelemetryLogger::fileHasContent(const std::string& filePath)
{
    FILE* fp = std::fopen(filePath.c_str(), "rb");
    if (!fp)
        return false;

    const int seekOk = std::fseek(fp, 0, SEEK_END);
    const long size = (seekOk == 0) ? std::ftell(fp) : 0;
    std::fclose(fp);
    return size > 0;
}

void TelemetryLogger::appendLinesToFile(const std::string& filePath, const std::string& header, const std::vector<std::string>& lines)
{
    if (lines.empty())
        return;

    const bool hasContent = fileHasContent(filePath);

    FILE* fp = std::fopen(filePath.c_str(), "ab");
    if (!fp)
        return;

    if (!hasContent)
        std::fprintf(fp, "%s\n", header.c_str());

    for (const std::string& line : lines)
        std::fprintf(fp, "%s\n", line.c_str());

    std::fclose(fp);
}
