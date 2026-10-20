#include "app/rendermonitor.h"
#include "app/renderthread.h"
#include "shell/displaysettings.h"
#include "shell/skinwindow.h"
#include <QStringList>

using fugue::app::TimingSummary;
namespace {
QString timing(const QString& label, const TimingSummary& value, bool details) {
    const auto number = [&](double ms) { return (value.samples ? QString::number(ms, 'f', 2) : QString("n/a")).rightJustified(8); };
    QString row = label.leftJustified(18) + number(value.average) + number(value.p95);
    if (details) row += QString::number(qulonglong(value.samples)).rightJustified(6);
    return row;
}
QString age(const TimingSummary& value) {
    return value.samples ? QString::number(value.ageMs / 1000, 'f', 1) : QString("n/a");
}
QStringList warningLines(const std::string& reason) {
    // Keep the explanation readable without making the numeric table wrap or expand to the map width.
    QStringList lines;
    QString line = "Fallback:";
    for (const auto& word : QString::fromStdString(reason).simplified().split(' ', Qt::SkipEmptyParts)) {
        if (line.size() + word.size() + 1 > 46) { lines << line; line.clear(); }
        if (!line.isEmpty()) line += ' ';
        line += word;
    }
    if (!line.isEmpty()) lines << line;
    return lines;
}
}

RenderMonitor::RenderMonitor(RenderThread& renderer, fugue::shell::SkinWindow& window)
    : renderer_(renderer), window_(window) {
    timer_.setInterval(500);
    connect(&timer_, &QTimer::timeout, this, &RenderMonitor::refresh);
    probe_.setInterval(fugue::app::EventLoopHealth::intervalMs);
    probe_.setTimerType(Qt::PreciseTimer);
    connect(&probe_, &QTimer::timeout, this, [this] {
        const double gap = double(lastProbe_.nsecsElapsed()) / 1e6;
        lastProbe_.restart();
        health_.sampleGap(gap);
    });
    connect(window_.displaySettings(), &fugue::shell::DisplaySettings::performanceOverlayChanged,
            this, &RenderMonitor::setEnabled);
    connect(window_.displaySettings(), &fugue::shell::DisplaySettings::performanceDetailsChanged,
            this, [this] { refresh(); });
    connect(&window_, &fugue::shell::SkinWindow::mapReady, this, [this](fugue::shell::MapWidget* map) {
        map->setPerformanceText(text_);
    });
    connect(&window_, &fugue::shell::SkinWindow::auxMapBuilt, this, [this](const QString& id) {
        if (auto* map = window_.auxMap(id); map && active()) {
            const auto key = id.toLower().toStdString();
            auto it = auxText_.find(key);
            map->setPerformanceText(it == auxText_.end() ? formatMap(key, {}, 0) : it->second);
        }
    });
    connect(&window_, &fugue::shell::SkinWindow::auxMapRemoved, this, [this](const QString& id) {
        const auto key = id.toLower().toStdString();
        auxText_.erase(key); previousFrames_.erase(key);
    });
    setEnabled(window_.displaySettings()->performanceOverlay());
}

void RenderMonitor::setEnabled(bool enabled) {
    if (enabled == active()) return;
    if (enabled) {
        previousFrames_.clear();
        for (const auto& [id, map] : renderer_.metrics().maps) previousFrames_[id] = map.frames;
        health_ = {};
        interval_.start(); lastProbe_.start();
        timer_.start(); probe_.start();
        refresh();
    } else {
        timer_.stop(); probe_.stop();
        text_.clear(); auxText_.clear();
        for (auto* map : window_.findChildren<fugue::shell::MapWidget*>()) map->setPerformanceText({});
    }
}

QString RenderMonitor::formatMap(const std::string& id, const fugue::app::MapMetrics& m, double rate) const {
    const bool details = window_.displaySettings()->performanceDetails();
    const bool requestedGpu = window_.displaySettings()->useGpu();
    const bool gpu = m.frames ? m.lastGpu : requestedGpu;
    QString status = m.frames ? (gpu ? "GPU" : "CPU") : "Waiting for frame";
    if (!m.frames || gpu != requestedGpu) status += requestedGpu ? " (GPU requested)" : " (CPU requested)";
    QStringList lines{QString("%1 | %2").arg(id.empty() ? "main" : QString::fromStdString(id).simplified(), status)};
    if (!m.fallbackReason.empty()) lines += warningLines(m.fallbackReason);
    lines << QString("Scene updates: %1/s").arg(rate, 0, 'f', 1);
    lines << (QString("Time (ms, <=120)").leftJustified(18) + QString("avg").rightJustified(8) +
              QString("p95").rightJustified(8) + (details ? QString("n").rightJustified(6) : QString()));
    const auto& render = gpu ? m.cpuSubmit : m.cpuRender;
    const auto lag = health_.lag.summary();
    lines << timing("Prepare", m.preparation, details)
          << timing(gpu ? "CPU submit" : "CPU render", render, details);
    if (gpu) lines << timing("GPU (delayed)", m.lastGpuMs >= 0 ? m.gpu : TimingSummary{}, details);
    lines << timing("UI delay", lag, details);
    QString health = QString("UI stalls >=50ms: %1").arg(qulonglong(health_.stalls));
    if (!details && render.samples && render.ageMs >= 1000) health += " | Render age: " + age(render) + "s";
    lines << health;
    if (!details) return lines.join('\n');

    if (gpu) {
        lines << "" << "CPU submit breakdown";
        static const char* phases[] = {"Acquire", "Record", "Compose", "Present"};
        static const char* recording[] = {"  Setup", "  Plan / uploads", "  Bindings", "  Encode", "  Retire"};
        for (size_t i = 0; i < m.phases.size(); ++i) {
            lines << timing(phases[i], m.phases[i], true);
            if (i == 1) for (size_t j = 0; j < m.recording.size(); ++j)
                lines << timing(recording[j], m.recording[j], true);
        }
    }
    lines << QString("Frames: %1 GPU | %2 CPU | %3 fallback").arg(m.gpuFrames).arg(m.cpuFrames).arg(m.fallbacks)
          << QString("Prepared: %1 | Skipped: %2 | Dropped: %3").arg(m.prepared).arg(m.unchanged).arg(m.dropped)
          << QString("Age (s): prep %1 | render %2%3").arg(age(m.preparation), age(render), gpu ? " | GPU " + age(m.gpu) : QString());
    if (lag.samples) lines << QString("UI delay max: %1 ms").arg(lag.maximum, 0, 'f', 1);
    if (gpu && !m.backend.empty()) lines << "Device: " + QString::fromStdString(m.backend).simplified();
    lines << "CPU wall time; indented rows are nested.";
    if (gpu) lines << "CPU and GPU overlap; don't add timings.";
    return lines.join('\n');
}

void RenderMonitor::refresh() {
    if (!active()) return;
    auto metrics = renderer_.metrics();
    metrics.maps.try_emplace(""); // show a waiting main-map panel before any work has arrived
    const double seconds = double(interval_.nsecsElapsed()) / 1e9;
    interval_.restart();
    std::map<std::string, uint64_t> next;
    auxText_.clear();
    for (const auto& [id, map] : metrics.maps) {
        const auto previous = previousFrames_.find(id);
        const uint64_t before = previous == previousFrames_.end() ? 0 : previous->second;
        const double rate = seconds > 0 && map.frames >= before ? double(map.frames - before) / seconds : 0;
        next[id] = map.frames;
        auto text = formatMap(id, map, rate);
        if (id.empty()) {
            text_ = text;
            if (window_.map()) window_.map()->setPerformanceText(text);
        } else {
            auxText_[id] = text;
            if (auto* widget = window_.auxMap(QString::fromStdString(id))) widget->setPerformanceText(text);
        }
    }
    previousFrames_ = std::move(next);
}
