#pragma once
#include <opencv2/core.hpp>
#include <vector>
#include <optional>

namespace metrology {
struct Settings { int halfLength=25, halfWidth=3; double sigma=1.2, threshold=10; int polarity=0, selection=0; };
struct Probe { cv::Point2d center, direction; };
struct Edge { cv::Point2d point; double amplitude; };
struct Circle { cv::Point2d center; double radius=0, peakToValley=0, rms=0; std::vector<double> residuals; };
struct MinimumZone { cv::Point2d center; double innerRadius=0, outerRadius=0; bool converged=false; double width() const { return outerRadius-innerRadius; } };
struct Coverage { double percent=0, maxGapDegrees=360; int valid=0, total=0; };
struct SplineSettings { double cutoffUpr=50, tension=0, maxGapDegrees=5; };
struct FilteredProfile {
    cv::Point2d center;
    std::vector<double> inputRadii;
    std::vector<cv::Point2d> points;
    double maxGapDegrees=0;
};
std::vector<double> periodicSpline(const std::vector<double> &values, double cutoffUpr, double tension);
FilteredProfile filterRadialProfile(const std::vector<cv::Point2d> &points, cv::Point2d center,
                                    int sampleCount, const SplineSettings &settings);
MinimumZone minimumZone(const std::vector<cv::Point2d> &points, const Circle &lsc);
Coverage coverage(const std::vector<bool> &found);
Circle minimumEnclosingCircle(const std::vector<cv::Point2d> &points);
// Ordered simple polygon; radius is accurate to tolerancePx on the polygon.
Circle maximumInscribedCircle(const std::vector<cv::Point2d> &contour, double tolerancePx=0.001);
struct Calibration { double pixelsPerMm=0, distance=0, rms=0, angleDegrees=0; };
std::optional<Edge> extract(const cv::Mat &gray, const Probe &, const Settings &);
Circle fitCircle(const std::vector<cv::Point2d> &points);
Circle circleThroughThreePoints(cv::Point2d a, cv::Point2d b, cv::Point2d c);
Calibration calibrate(const std::vector<cv::Point2d> &a, const std::vector<cv::Point2d> &b, double lengthMm);
}
