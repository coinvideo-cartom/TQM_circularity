#include "mainwindow.h"
#include <QApplication>
#include <stdexcept>
#include <iostream>
#include <cmath>
#include <QTextStream>
class MeasurementUiTest {
public:
    static void run(MainWindow &w) {
        w.image=QImage(640,640,QImage::Format_Grayscale8);
        for(int y=0;y<640;++y)for(int x=0;x<640;++x){double a=std::atan2(y-320.,x-320.),r=std::hypot(x-320.,y-320.);w.image.scanLine(y)[x]=cv::saturate_cast<uchar>(30+200*0.5*(1+std::erf((r-200-1.5*std::cos(3*a))/1.4)));}
        w.gray=cv::Mat(640,640,CV_8UC1,w.image.bits(),w.image.bytesPerLine()).clone();
        w.pixelsPerMm=20;w.calibrationSize=w.image.size();w.scaleLabel->setText("Scala test: 20 px/mm");w.start(2);
        w.clicks={{520,320},{320,520},{120,320}};w.run();
        if(!w.circle || !w.zone || !w.result->text().contains("Rotondita MZC") || !w.result->text().contains("100.0%"))throw std::runtime_error("Missing calibrated metrics");
        if(w.zone->width()<2.8 || w.zone->width()>3.2)throw std::runtime_error("End-to-end MZ width");
        if(!w.inscribed || !w.circumscribed || !w.result->text().contains("Differenza radiale MCC-MIC"))throw std::runtime_error("Missing MIC/MCC");
        auto verifyCsv=[&](bool calibrated,bool mic){QString csv;QTextStream stream(&csv);w.writeCsv(stream);stream.flush();auto lines=csv.trimmed().split('\n');auto header=lines[0].split(',');auto row=lines[1].split(',');if(header.size()!=row.size() || lines.size()!=int(w.points.size()+1))throw std::runtime_error("CSV shape");int delta=header.indexOf("delta_radius_mcc_mic_px");if(delta<0 || row[delta].isEmpty()==mic)throw std::runtime_error("CSV MIC availability");if(mic && std::abs(row[delta].toDouble()-(w.circumscribed->radius-w.inscribed->radius))>1e-8)throw std::runtime_error("CSV radial difference");if(calibrated){int um=header.indexOf("delta_radius_mcc_mic_um");if(um<0 || (mic && std::abs(row[um].toDouble()-row[delta].toDouble()*1000/w.pixelsPerMm)>1e-6))throw std::runtime_error("CSV units");}
            const auto verifyRadius=[&](const char *name, std::optional<double> radiusPx){
                int column=header.indexOf(name);
                if(!calibrated){
                    if(column>=0)throw std::runtime_error("Unexpected calibrated radius column");
                    return;
                }
                if(column<0 || row[column].isEmpty()!=!radiusPx)
                    throw std::runtime_error("CSV radius availability");
                if(radiusPx && std::abs(row[column].toDouble()-*radiusPx/w.pixelsPerMm)>1e-8)
                    throw std::runtime_error("CSV radius conversion");
            };
            verifyRadius("radius_mzc_mm",cv::norm(w.points.front()-w.zone->center));
            verifyRadius("radius_lsc_mm",w.circle->radius);
            verifyRadius("mzc_inner_radius_mm",w.zone->innerRadius);
            verifyRadius("mzc_outer_radius_mm",w.zone->outerRadius);
            verifyRadius("radius_mcc_mm",w.circumscribed->radius);
            verifyRadius("radius_mic_mm",mic?std::optional<double>(w.inscribed->radius):std::nullopt);
        };
        verifyCsv(true,true);
        w.filterEnabled->setChecked(true);
        w.filterCutoff->setValue(3);
        w.run();
        if(!w.filtered || !w.filtered->inscribed || std::abs(w.filtered->zone.width()-1.5)>0.12)
            throw std::runtime_error("Filtered end-to-end MZ width");
        auto verifyFilteredCsv=[&]{
            QString csv;QTextStream stream(&csv);w.writeCsv(stream);stream.flush();
            auto lines=csv.trimmed().split('\n'), header=lines.front().split(',');
            if(lines.size()!=int(w.points.size()+w.filtered->profile.points.size()+1))
                throw std::runtime_error("Filtered CSV rows");
            int type=header.indexOf("profile_type"), diameter=header.indexOf("diameter_lsc_px");
            if(type<0 || diameter<0)throw std::runtime_error("Filtered CSV headers");
            for(int i=1;i<lines.size();++i){
                auto row=lines[i].split(',');
                if(row.size()!=header.size())throw std::runtime_error("Filtered CSV column count");
                bool isFiltered=i>int(w.points.size());
                if(row[type]!=(isFiltered?"filtered":"acquired") || (isFiltered && !row[0].isEmpty()))
                    throw std::runtime_error("Filtered CSV provenance");
                double expected=2*(isFiltered?w.filtered->circle.radius:w.circle->radius);
                if(std::abs(row[diameter].toDouble()-expected)>1e-8)throw std::runtime_error("Filtered CSV metrics");
                int mm=header.indexOf("diameter_lsc_mm");
                if(w.pixelsPerMm>0 && (mm<0 || std::abs(row[mm].toDouble()-expected/w.pixelsPerMm)>1e-8))
                    throw std::runtime_error("Filtered CSV calibration");
            }
        };
        verifyFilteredCsv();
        w.pixelsPerMm=0;verifyFilteredCsv();w.pixelsPerMm=20;
        w.view->fitInView(w.scene->sceneRect(),Qt::KeepAspectRatio);QApplication::processEvents();w.grab().save("measurement-filter-ui.png");
        w.number->setValue(7200);w.run();
        if(!w.filtered || w.filtered->profile.points.size()!=7200 || w.found.size()!=7200)
            throw std::runtime_error("Filtered 7200 extractors");
        verifyFilteredCsv();
        w.filterCutoff->setValue(3599);w.number->setValue(360);w.run();
        if(w.filtered || w.circle || !w.result->text().contains("Spline"))
            throw std::runtime_error("Invalid filter retains exportable result");
        w.filterEnabled->setChecked(false);w.run();
        verifyCsv(true,true);
        w.view->fitInView(w.scene->sceneRect(),Qt::KeepAspectRatio);QApplication::processEvents();w.grab().save("measurement-ui.png");
        w.pixelsPerMm=0;w.run();if(!w.circle || !w.result->text().contains("px"))throw std::runtime_error("Missing pixel metrics");verifyCsv(false,true);
        w.number->setValue(60);w.run();if(!w.circle || !w.circumscribed || w.inscribed || !w.result->text().contains("intervallo edge > 5"))throw std::runtime_error("MIC gap guard");verifyCsv(false,false);
        w.pixelsPerMm=20;verifyCsv(true,false);
        w.threshold->setValue(255);w.run();if(w.circle || w.zone || !w.result->text().contains("Copertura insufficiente"))throw std::runtime_error("Invalid coverage retained results");
        w.start(2);if(w.circle || w.zone)throw std::runtime_error("Selection failed to invalidate");
    }
};
int main(int argc,char **argv){QApplication app(argc,argv);try{MainWindow w;w.show();app.processEvents();MeasurementUiTest::run(w);std::cout<<"PASS: calibrated/pixel circle metrics, plot, coverage failure, invalidation\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
