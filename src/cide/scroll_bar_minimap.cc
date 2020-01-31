// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/scroll_bar_minimap.h"

#include <QPainter>
#include <QPaintEvent>

#include "cide/document.h"
#include "cide/document_range.h"
#include "cide/document_widget.h"
#include "cide/qt_thread.h"
#include "cide/text_utils.h"

ScrollbarMinimap::ScrollbarMinimap(const std::shared_ptr<Document>& document, DocumentWidget* widget, int width, QWidget* parent)
  : QWidget(parent),
    mapWidth(width),
    document(document),
    widget(widget) {
  setAutoFillBackground(false);
  
  mExit = false;
  haveRequest = false;
  mapUpdateThread.reset(new std::thread(&ScrollbarMinimap::MapUpdateThreadMain, this));
}

ScrollbarMinimap::~ScrollbarMinimap() {
  updateRequestMutex.lock();
  mExit = true;
  updateRequestMutex.unlock();
  newUpdateRequestCondition.notify_all();
  // Abort RunInQtThreadBlocking() that may be called by the mapUpdateThread.
  abortData.Abort();
  
  mapUpdateThread->join();
  mapUpdateThread = nullptr;
}

void ScrollbarMinimap::UpdateMap(const std::vector<DocumentRange>& layoutLines, const std::shared_ptr<Document>& documentCopy) {
  // Update the scroll range
  maxScroll = layoutLines.size() - 1;
  
  std::unique_lock<std::mutex> lock(updateRequestMutex);
  // Copy the document and pass the copy to the background thread which updates the map
  if (documentCopy) {
    requestDocument = documentCopy;
  } else {
    requestDocument.reset(new Document());
    requestDocument->AssignTextAndStyles(*document);
  }
  requestLayout = layoutLines;
  haveRequest = true;
  newUpdateRequestCondition.notify_one();
}

void ScrollbarMinimap::SetDiffLines(const std::vector<LineDiff>& diffLines) {
  std::vector<DiffLine> newDiffLines;
  std::vector<int> newDiffRemovals;
  
  for (const LineDiff& diff : diffLines) {
    if (diff.type == LineDiff::Type::Removed) {
      newDiffRemovals.push_back(diff.line);
    } else {
      newDiffLines.emplace_back(
          diff.line,
          diff.line + diff.numLines - 1,
          (diff.type == LineDiff::Type::Added) ? qRgb(0, 255, 0) : qRgb(255, 255, 0));
    }
  }
  
  this->diffLines.swap(newDiffLines);
  diffRemovals.swap(newDiffRemovals);
  
  update(rect());
}

void ScrollbarMinimap::paintEvent(QPaintEvent* event) {
  // Start painting
  QPainter painter(this);
  QRect rect = event->rect();
  painter.setClipRect(rect);
  
  // Draw left border
  painter.setPen(qRgb(80, 80, 80));
  painter.drawLine(0, 0, 0, height());
  
  // Draw minimap.
  int mapRenderHeight = GetMapRenderHeight();
  if (!map.isNull()) {
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(
        QRect(1, 0, mapWidth, mapRenderHeight),
        map,
        map.rect());
  }
  
  // Draw background below minimap
  if (mapRenderHeight < height()) {
    painter.setBrush(QBrush(qRgb(127, 127, 127)));
    painter.setPen(Qt::NoPen);
    painter.drawRect(1, mapRenderHeight, mapWidth, height() - mapRenderHeight);
  }
  
  if (!map.isNull()) {
    // Draw diff lines.
    for (const DiffLine& diffLine : diffLines) {
      constexpr int kAdditionalLineExtent = 0;
      int y0 = (mapRenderHeight * (diffLine.firstLine - kAdditionalLineExtent + 0.5f) * widget->GetLineHeight()) / (widget->GetLineHeight() * map.height()) + 0.5f;
      int y1 = (mapRenderHeight * (diffLine.lastLine + kAdditionalLineExtent + 0.5f) * widget->GetLineHeight()) / (widget->GetLineHeight() * map.height()) + 0.5f;
      
      painter.setPen(Qt::NoPen);
      painter.setBrush(QBrush(diffLine.color));
      painter.drawRect(1, std::max(0, y0 - 1), 3, std::min(height() - (y0 - 1), (y1 - y0) + 2));
    }
    
    // Draw diff removals.
    for (int diffRemoval : diffRemovals) {
      int y = (mapRenderHeight * (diffRemoval + 0.5f) * widget->GetLineHeight()) / (widget->GetLineHeight() * map.height()) + 0.5f;
      
      painter.setPen(Qt::NoPen);
      painter.setBrush(QBrush(qRgb(255, 0, 0)));
      painter.drawEllipse(1, std::max(0, y - 1), 3, 3);
    }
    
    // Draw line attribute lines.
    for (const MapLine& mapLine : mapLines) {
      int y = (mapRenderHeight * (mapLine.line + 0.5f) * widget->GetLineHeight()) / (widget->GetLineHeight() * map.height()) + 0.5f;
      
      painter.setPen(qRgb(200, 200, 200));
      painter.setBrush(QBrush(mapLine.color));
      painter.drawRect(1, std::max(0, y - 1), mapWidth - 1, 3);
    }
    
    // Draw the visible window.
    int windowStart = (mapRenderHeight * widget->GetYScroll()) / (widget->GetLineHeight() * map.height());
    int windowEnd = (mapRenderHeight * (widget->GetYScroll() + widget->height() + 0.5f * widget->GetLineHeight())) / (widget->GetLineHeight() * map.height());
    
    painter.setPen(Qt::NoPen);
    painter.setBrush(QBrush(qRgb(0, 0, 255)));
    painter.setOpacity(0.2f);
    painter.drawRect(1, windowStart, mapWidth - 1, windowEnd - windowStart);
    
    painter.setPen(qRgb(0, 0, 255));
    painter.setBrush(Qt::NoBrush);
    painter.setOpacity(1.f);
    painter.drawRect(1, windowStart, mapWidth - 1, windowEnd - windowStart);
    
    painter.end();
  }
}

void ScrollbarMinimap::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton) {
    SetScroll(event->y());
  }
}

void ScrollbarMinimap::mouseMoveEvent(QMouseEvent* event) {
  if ((event->buttons() & Qt::LeftButton) || (event->buttons() & Qt::MiddleButton)) {
    SetScroll(event->y());
  }
}

QSize ScrollbarMinimap::sizeHint() const {
  return QSize(mapWidth + 1, 100);
}

QSize ScrollbarMinimap::minimumSizeHint() const {
  return QSize(mapWidth + 1, 0);
}

void ScrollbarMinimap::SetScroll(int clickY) {
  int mapRenderHeight = GetMapRenderHeight();
  if (mapRenderHeight == 0) {
    return;
  }
  int targetScroll = maxScroll * widget->GetLineHeight() * clickY / static_cast<float>(mapRenderHeight) - 0.5f * widget->height() + 0.5f;
  targetScroll = std::max(0, std::min(widget->GetLineHeight() * maxScroll, targetScroll));
  widget->SetYScroll(targetScroll);
}

int ScrollbarMinimap::GetMapRenderHeight() {
  if (map.isNull()) {
    return 0;
  } else {
    float character_height_by_width = widget->GetLineHeight() / static_cast<float>(widget->GetCharWidth());
    return std::min(height(), static_cast<int>(character_height_by_width * map.height() + 0.5f));
  }
}

void ScrollbarMinimap::MapUpdateThreadMain() {
  // TODO: Synchronize with editor colors?
  QColor bookmarkColor = qRgb(0, 0, 255);
  QColor errorColor = qRgb(255, 0, 0);
  QColor warningColor = qRgb(0, 255, 0);
  
  while (true) {
    std::unique_lock<std::mutex> lock(updateRequestMutex);
    if (mExit) {
      return;
    }
    while (!haveRequest) {
      newUpdateRequestCondition.wait(lock);
      if (mExit) {
        return;
      }
    }
    
    std::shared_ptr<Document> workingDocument = requestDocument;
    requestDocument = nullptr;
    std::vector<DocumentRange> workingLayout;
    workingLayout.swap(requestLayout);
    haveRequest = false;
    
    lock.unlock();
    
    // Perform the update
    QImage newMap(mapWidth, workingLayout.size(), QImage::Format_RGB888);
    std::vector<MapLine> newMapLines;
    
    Document::CharacterAndStyleIterator it(workingDocument.get());
    for (int line = 0; line < workingLayout.size(); ++ line) {
      const DocumentRange& lineRange = workingLayout[line];
      while (it.GetCharacterOffset() < lineRange.start.offset) {
        ++ it;
      }
      
      uchar* ptr = newMap.bits() + line * newMap.bytesPerLine();
      
      // NOTE: This was changed to render these lines over the minimap during
      //       the paintEvent afterwards. This allows to easily keep them at a
      //       minimum width.
//       // If this is a special line (having a bookmark or a problem), color the
//       // whole line in blue or red/green.
//       int lineAttributes = workingDocument->lineAttributes(line);  // TODO: iterate over lines with LineIterator for faster access? Maybe add a way to initialize the character&style iterator quickly from the line iterator?
//       if (lineAttributes != 0) {
//         // <Determine color...>
//         for (int i = 0; i < mapWidth; ++ i) {
//           *ptr++ = lineColor.red();
//           *ptr++ = lineColor.green();
//           *ptr++ = lineColor.blue();
//         }
//         continue;
//       }
      
      // Render the line characters
      int charactersInLine = 0;
      while (it.GetCharacterOffset() < lineRange.end.offset && charactersInLine < mapWidth) {
        if (!it.IsValid()) {
          qDebug() << "ERROR: Character iterator became invalid while iterating until the line end (according to the layout). Is there a mismatch between the document and the layout?";
          break;
        }
        if (IsWhitespace(it.GetChar())) {
          *ptr++ = 255;
          *ptr++ = 255;
          *ptr++ = 255;
        } else {
          const HighlightRange& style = it.GetStyle();
          // Blend the character color with the background color to make the
          // text rendering look less "heavy". This also makes it look more like
          // a zoomed-out version of the actual text since only a small percentage
          // of the character rectangles is taken up by the character color, so
          // it is expected that a lot of the background color is blended in.
          constexpr float kDampenFactor = 0.45f;
          *ptr++ = (255 * (1 - kDampenFactor) + style.textColor.red() * kDampenFactor) + 0.5f;
          *ptr++ = (255 * (1 - kDampenFactor) + style.textColor.green() * kDampenFactor) + 0.5f;
          *ptr++ = (255 * (1 - kDampenFactor) + style.textColor.blue() * kDampenFactor) + 0.5f;
        }
        ++ charactersInLine;
        ++ it;
      }
      
      // Render the line background color to the right of the characters
      while (charactersInLine < mapWidth) {
        *ptr++ = 255;
        *ptr++ = 255;
        *ptr++ = 255;
        ++ charactersInLine;
      }
    }
    
    // Collect all places where lines should be drawn over the minimap.
    int line = 0;
    Document::LineIterator lineIt(workingDocument.get());
    while (lineIt.IsValid()) {
      int lineAttributes = lineIt.GetAttributes();
      if (lineAttributes != 0) {
        float red = 0;
        float green = 0;
        float blue = 0;
        int bgColorCount = 0;
        // The checks are done in order of preference for the case that there are
        // multiple flags for a single line.
        if (lineAttributes & static_cast<int>(LineAttribute::Bookmark)) {
          red += bookmarkColor.red();
          green += bookmarkColor.green();
          blue += bookmarkColor.blue();
          ++ bgColorCount;
        }
        if (lineAttributes & static_cast<int>(LineAttribute::Error)) {
          red += errorColor.red();
          green += errorColor.green();
          blue += errorColor.blue();
          ++ bgColorCount;
        }
        if (lineAttributes & static_cast<int>(LineAttribute::Warning)) {
          red += warningColor.red();
          green += warningColor.green();
          blue += warningColor.blue();
          ++ bgColorCount;
        }
        QColor lineColor = qRgb(red / bgColorCount, green / bgColorCount, blue / bgColorCount);
        newMapLines.emplace_back(line, lineColor);
      }
      
      ++ lineIt;
      ++ line;
    }
    
    // Transfer the new map to the main (Qt) thread and make it update the display.
    RunInQtThreadBlocking([&]() {
      // If the document has been closed in the meantime, we must not access its
      // widget anymore.
      if (mExit) {
        return;
      }
      
      // It used to be necessary to destruct the document in the main thread
      // because of the file watcher that it creates. This should not be
      // necessary anymore now that the file watcher is created lazily, but we
      // still do it here to be on the safe side.
      workingDocument.reset();
      
      map = newMap;
      mapLines.swap(newMapLines);
      update(rect());
    }, &abortData);
  }
}
