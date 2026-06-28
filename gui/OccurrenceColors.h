#pragma once

#include <QColor>
#include <cstddef>

inline QColor occurrenceColor(size_t index)
{
    constexpr size_t hueStep = 137;
    return QColor::fromHsv(static_cast<int>((index * hueStep) % 360), 110, 255);
}
