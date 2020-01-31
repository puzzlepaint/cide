// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <QWidget>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <thread>

#include "cide/qt_thread.h"

class Document;
struct DocumentRange;
class DocumentWidget;
struct LineDiff;

class ScrollbarMinimap : public QWidget {
 public:
  ScrollbarMinimap(const std::shared_ptr<Document>& document, DocumentWidget* widget, int width, QWidget* parent = nullptr);
  ~ScrollbarMinimap();
  
  /// Note: documentCopy can be null. In this case, ScrollbarMinimap will do the copy itself.
  void UpdateMap(const std::vector<DocumentRange>& layoutLines, const std::shared_ptr<Document>& documentCopy);
  
  void SetDiffLines(const std::vector<LineDiff>& diffLines);
  
 protected:
  void paintEvent(QPaintEvent* event) override;
  
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  
  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;
  
 private:
  int GetMapRenderHeight();
  void SetScroll(int clickY);
  
  void MapUpdateThreadMain();
  
  
  struct MapLine {
    inline MapLine(int line, QColor color)
        : line(line),
          color(color) {};
    
    /// The text line at which this line should be drawn
    int line;
    
    /// The color of this line
    QColor color;
  };
  
  struct DiffLine {
    inline DiffLine(int firstLine, int lastLine, QColor color)
        : firstLine(firstLine),
          lastLine(lastLine),
          color(color) {};
    
    /// The first line
    int firstLine;
    
    /// The last line
    int lastLine;
    
    /// The color of this line
    QColor color;
  };
  
  QImage map;
  int mapWidth;
  std::vector<MapLine> mapLines;
  std::vector<DiffLine> diffLines;
  std::vector<int> diffRemovals;
  
  std::unique_ptr<std::thread> mapUpdateThread;
  std::atomic<bool> mExit;
  RunInQtThreadAbortData abortData;
  std::condition_variable newUpdateRequestCondition;
  std::mutex updateRequestMutex;
  std::atomic<bool> haveRequest;
  std::shared_ptr<Document> requestDocument;
  std::vector<DocumentRange> requestLayout;
  
  int maxScroll = 0;
  
  std::shared_ptr<Document> document;
  DocumentWidget* widget;
};
