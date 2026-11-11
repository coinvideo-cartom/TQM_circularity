#pragma once
#include <QMainWindow>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QCheckBox>
#include <functional>
#include "metrology.h"
class QTextStream;
class ImageView : public QGraphicsView {
public:
    using QGraphicsView::QGraphicsView;
    std::function<void(QPointF)> clicked;
protected:
    void mousePressEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
};
class MainWindow : public QMainWindow {
    friend class MeasurementUiTest;
public:
    explicit MainWindow(QWidget *parent=nullptr);
private:
    ImageView *view;
    QGraphicsScene *scene;
    QDoubleSpinBox *sigma,*threshold,*lengthMm;
    QSpinBox *halfLength,*halfWidth,*number;
    QCheckBox *filterEnabled;
    QDoubleSpinBox *filterCutoff,*filterTension,*filterGap;
    struct FilteredMeasurement {
        metrology::SplineSettings settings;
        metrology::FilteredProfile profile;
        metrology::Circle circle, circumscribed;
        metrology::MinimumZone zone;
        std::optional<metrology::Circle> inscribed;
    };
    std::optional<FilteredMeasurement> filtered;
    QComboBox *polarity,*selection,*direction;
    QLabel *instructions,*result,*scaleLabel;
    QWidget *residualPlot;
    cv::Mat gray;
    QImage image;
    QVector<QPointF> clicks;
    std::vector<metrology::Probe> probes;
    std::vector<cv::Point2d> points;
    std::vector<bool> found;
    std::optional<metrology::Circle> circle;
    std::optional<metrology::MinimumZone> zone;
    std::optional<metrology::Circle> inscribed, circumscribed;
    double pixelsPerMm=0;
    QSize calibrationSize;
    int mode=0;
    metrology::Settings settings() const;
    void openImage();
    void start(int newMode);
    void run();
    void redraw();
    void exportCsv();
    void writeCsv(QTextStream &out) const;
    void calibrationFile(bool save);
};
