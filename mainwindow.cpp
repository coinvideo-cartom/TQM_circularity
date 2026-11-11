#include "mainwindow.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFileDialog>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QSplitter>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QGraphicsPixmapItem>
#include <QPainter>
#include <QScrollArea>
#include <algorithm>
#include <cmath>
#include <opencv2/imgcodecs.hpp>

class ResidualPlot : public QWidget {
public:
    std::vector<QPointF> samples;
    std::vector<QPointF> filteredSamples;
    QString unit="px";
    explicit ResidualPlot(QWidget *parent=nullptr):QWidget(parent){setMinimumHeight(180);}
    QSize sizeHint() const override {return QSize(600,240);}
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),QColor("#fafbfc"));
        p.setPen(Qt::black);p.drawText(QRect(12,4,width()-24,24),Qt::AlignLeft,QString("Scostamento dal LSC acquisito [%1] - Blu: acquisito | Viola: filtrato").arg(unit));
        QRectF box(80,35,std::max(1,width()-110),std::max(1,height()-75));
        if(samples.empty()){p.drawText(box,Qt::AlignCenter,"Eseguire una misura del cerchio per visualizzare il profilo.");return;}
        double extent=0;for(auto q:samples)extent=std::max(extent,std::abs(q.y()));for(auto q:filteredSamples)extent=std::max(extent,std::abs(q.y()));extent=std::max(extent*1.15,1e-6);
        for(int j=-2;j<=2;++j){double v=extent*j/2,y=box.center().y()-v/extent*box.height()/2;p.setPen(QColor("#d5dbe1"));p.drawLine(QPointF(box.left(),y),QPointF(box.right(),y));p.setPen(Qt::black);p.drawText(QRectF(0,y-10,74,20),Qt::AlignRight|Qt::AlignVCenter,QString::number(v,'g',4));}
        for(int a=0;a<=360;a+=60){double x=box.left()+a*box.width()/360;p.setPen(QColor("#d5dbe1"));p.drawLine(QPointF(x,box.top()),QPointF(x,box.bottom()));p.setPen(Qt::black);p.drawText(QRectF(x-20,box.bottom()+5,40,20),Qt::AlignCenter,QString::number(a));}
        p.setPen(QColor("#176dba"));p.setBrush(QColor("#176dba"));
        // Individual samples deliberately do not bridge unmeasured angular sectors.
        for(auto q:samples)p.drawEllipse(QPointF(box.left()+q.x()*box.width()/360,box.center().y()-q.y()/extent*box.height()/2),2,2);
        if(!filteredSamples.empty()){
            QPolygonF line;
            for(auto q:filteredSamples)line<<QPointF(box.left()+q.x()*box.width()/360,box.center().y()-q.y()/extent*box.height()/2);
            line<<QPointF(box.right(),line.front().y());
            p.setPen(QPen(QColor("#a020c0"),1.5));p.setBrush(Qt::NoBrush);p.drawPolyline(line);
        }
    }
};
void ImageView::mousePressEvent(QMouseEvent *e) {
    if(e->button()==Qt::LeftButton && clicked) clicked(mapToScene(e->pos()));
    else QGraphicsView::mousePressEvent(e);
}
void ImageView::wheelEvent(QWheelEvent *e) { double f=e->angleDelta().y()>0?1.2:1/1.2; scale(f,f); }
MainWindow::MainWindow(QWidget *parent):QMainWindow(parent) {
    setWindowTitle("TQM | Misura circonferenza"); resize(1300,850);
    auto split=new QSplitter(this); setCentralWidget(split);
    auto panel=new QWidget; auto layout=new QVBoxLayout(panel); auto scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setWidget(panel);scroll->setMinimumWidth(380);scroll->setMaximumWidth(430);split->addWidget(scroll);
    auto button=[&](QString title,auto action) { auto b=new QPushButton(title); layout->addWidget(b); connect(b,&QPushButton::clicked,this,action); };
    button("Apri immagine",[this]{openImage();});
    button("Adatta immagine",[this]{view->fitInView(scene->sceneRect(),Qt::KeepAspectRatio);});
    button("1. Seleziona i due lati del blocchetto",[this]{start(1);});
    button("2. Seleziona circonferenza",[this]{start(2);});
    instructions=new QLabel("Aprire un'immagine. Zoom con la rotella."); instructions->setWordWrap(true); layout->addWidget(instructions);
    auto form=new QFormLayout; layout->addLayout(form);
    auto ds=[&](QString label,double min,double max,double value,int digits) {auto w=new QDoubleSpinBox;w->setRange(min,max);w->setDecimals(digits);w->setValue(value);form->addRow(label,w);return w;};
    auto is=[&](QString label,int min,int max,int value) {auto w=new QSpinBox;w->setRange(min,max);w->setValue(value);form->addRow(label,w);return w;};
    lengthMm=ds("Blocchetto [mm]",0.001,10000,10,4);
    halfLength=is("Semilunghezza ROI [px]",5,500,25); halfWidth=is("Semilarghezza ROI [px]",0,100,3);
    sigma=ds("Sigma [px]",0.4,30,1.2,2); threshold=ds("Soglia gradiente [grigi/px]",0.1,255,10,2);
    number=is("Numero estrattori",12,7200,360);
    polarity=new QComboBox;polarity->addItems({"Entrambe","Scuro -> chiaro","Chiaro -> scuro"});form->addRow("Polarita",polarity);
    selection=new QComboBox;selection->addItems({"Piu forte","Primo","Ultimo"});form->addRow("Edge",selection);
    direction=new QComboBox;direction->addItems({"Direzione normale / esterno","Direzione inversa / interno"});form->addRow("Scansione",direction);
    filterEnabled=new QCheckBox("Spline periodica ISO 16610-22:2015");form->addRow(filterEnabled);
    filterEnabled->setToolTip("Equazioni periodiche ISO 16610-22:2015. Applicato al profilo radiale della circonferenza.");
    filterCutoff=ds("Taglio spline [UPR]",0.01,3599,50,2);
    filterTension=ds("Tensione spline beta",0,1,0,4);
    filterGap=ds("Intervallo max [gradi]",0.01,180,5,2);
    filterGap->setToolTip("Massima distanza angolare tra punti acquisiti, inclusa la chiusura del profilo.");
    for(auto widget:{filterCutoff,filterTension,filterGap})widget->setEnabled(false);
    button("Esegui / aggiorna misura",[this]{run();});
    scaleLabel=new QLabel("Scala: non calibrata (risultati in px)");scaleLabel->setWordWrap(true);layout->addWidget(scaleLabel);
    button("Salva calibrazione",[this]{calibrationFile(true);});
    button("Carica calibrazione",[this]{calibrationFile(false);});
    button("Azzera calibrazione",[this]{pixelsPerMm=0;scaleLabel->setText("Scala: non calibrata");start(0);});
    result=new QLabel;result->setWordWrap(true);result->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(result);
    button("Esporta punti e scostamenti CSV",[this]{exportCsv();});layout->addStretch();
    scene=new QGraphicsScene(this);view=new ImageView(scene);view->setRenderHint(QPainter::Antialiasing);view->setBackgroundBrush(QColor("#20252b"));auto right=new QSplitter(Qt::Vertical);right->addWidget(view);residualPlot=new ResidualPlot;right->addWidget(residualPlot);view->setMinimumHeight(260);right->setStretchFactor(0,3);right->setStretchFactor(1,1);right->setSizes({580,240});split->addWidget(right);
    view->clicked=[this](QPointF p){if(gray.empty() || !QRectF(image.rect()).contains(p) || mode==0) return;int limit=mode==1?4:3;if(clicks.size()>=limit) return;clicks.push_back(p);redraw();instructions->setText(QString("Punti selezionati: %1/%2. Al termine premere Esegui.").arg(clicks.size()).arg(limit));};
    auto invalidate=[this]{points.clear();circle.reset();zone.reset();inscribed.reset();circumscribed.reset();filtered.reset();probes.clear();found.clear();result->setText("Parametri modificati: premere Esegui.");redraw();};
    for(auto w:{sigma,threshold,lengthMm,filterCutoff,filterTension,filterGap}) connect(w,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[invalidate](double){invalidate();});
    connect(filterEnabled,&QCheckBox::toggled,this,[this,invalidate](bool enabled){
        for(auto widget:{filterCutoff,filterTension,filterGap})widget->setEnabled(enabled);
        invalidate();
    });
    for(auto w:{halfLength,halfWidth,number}) connect(w,qOverload<int>(&QSpinBox::valueChanged),this,[invalidate](int){invalidate();});
    for(auto w:{polarity,selection,direction}) connect(w,qOverload<int>(&QComboBox::currentIndexChanged),this,[invalidate](int){invalidate();});
}
metrology::Settings MainWindow::settings() const {return {halfLength->value(),halfWidth->value(),sigma->value(),threshold->value(),polarity->currentIndex()==2?-1:polarity->currentIndex(),selection->currentIndex()};}
void MainWindow::openImage() {
    auto name=QFileDialog::getOpenFileName(this,"Apri immagine",{},"Immagini (*.png *.jpg *.jpeg *.bmp *.tif *.tiff);;Tutti i file (*)");if(name.isEmpty()) return;
    QImage loaded(name);
    if(loaded.isNull()) {
        QFile file(name);
        if(file.open(QIODevice::ReadOnly)) {
            auto bytes=file.readAll();
            try {
                auto decoded=cv::imdecode(std::vector<uchar>(bytes.begin(),bytes.end()),cv::IMREAD_GRAYSCALE);
                if(!decoded.empty()) loaded=QImage(decoded.data,decoded.cols,decoded.rows,int(decoded.step),QImage::Format_Grayscale8).copy();
            } catch(const cv::Exception &) {}
        }
    }
    if(loaded.isNull()){QMessageBox::warning(this,"Immagine","Impossibile leggere l'immagine.");return;}
    image=loaded.convertToFormat(QImage::Format_Grayscale8);gray=cv::Mat(image.height(),image.width(),CV_8UC1,image.bits(),image.bytesPerLine()).clone();
    if(pixelsPerMm>0 && calibrationSize!=image.size()){pixelsPerMm=0;scaleLabel->setText("Scala azzerata: risoluzione diversa.");}
    start(0);view->fitInView(scene->sceneRect(),Qt::KeepAspectRatio);
}
void MainWindow::start(int m) {mode=m;clicks.clear();probes.clear();points.clear();found.clear();circle.reset();zone.reset();inscribed.reset();circumscribed.reset();filtered.reset();result->clear();instructions->setText(m==1?"4 click: estremi del primo lato, poi estremi del lato opposto. Le frecce sono perpendicolari ai lati. Evitare gli spigoli.":m==2?"3 click su punti distinti del bordo della circonferenza, ben distanziati tra loro.":"Selezionare calibrazione o circonferenza. Scala valida solo con ottica, piano e risoluzione invariati.");redraw();}
void MainWindow::run() {
    points.clear();probes.clear();found.clear();circle.reset();zone.reset();inscribed.reset();circumscribed.reset();filtered.reset();result->clear();
    if(mode==1){pixelsPerMm=0;scaleLabel->setText("Scala: calibrazione da verificare.");}
    try {
        if(gray.empty() || clicks.size()!=(mode==1?4:3) || mode==0) throw std::runtime_error("Completare prima la selezione sull'immagine.");
        auto s=settings();if(s.halfLength<=std::ceil(3*s.sigma)+2) throw std::runtime_error("ROI troppo corta per il sigma scelto.");
        auto cvp=[](QPointF p){return cv::Point2d(p.x(),p.y());};
        auto sample=[&](metrology::Probe p,std::vector<cv::Point2d>& out){probes.push_back(p);auto e=metrology::extract(gray,p,s);found.push_back(bool(e));if(e){out.push_back(e->point);points.push_back(e->point);}};
        if(mode==1) {
            std::vector<cv::Point2d> sides[2];
            for(int side=0;side<2;++side){auto a=cvp(clicks[2*side]),b=cvp(clicks[2*side+1]);auto t=b-a;if(cv::norm(t)<10) throw std::runtime_error("Selezionare un lato lungo almeno 10 px.");t/=cv::norm(t);cv::Point2d n(-t.y,t.x);if(direction->currentIndex())n=-n;for(int i=0;i<number->value();++i) sample({a+(b-a)*((i+0.5)/number->value()),n},sides[side]);}
            if(sides[0].size()<size_t(std::ceil(number->value()*0.8)) || sides[1].size()<size_t(std::ceil(number->value()*0.8))) throw std::runtime_error("Calibrazione incompleta: meno dell'80% degli edge su un lato.");
            auto c=metrology::calibrate(sides[0],sides[1],lengthMm->value());pixelsPerMm=c.pixelsPerMm;calibrationSize=image.size();
            scaleLabel->setText(QString("Scala: %1 px/mm | %2 mm/px").arg(pixelsPerMm,0,'f',6).arg(1/pixelsPerMm,0,'f',8));
            result->setText(QString("Calibrazione riuscita\nDistanza: %1 px\nRMS rette: %2 px\nAngolo tra lati: %3 gradi\nEdge: %4/%5").arg(c.distance,0,'f',4).arg(c.rms,0,'f',4).arg(c.angleDegrees,0,'f',3).arg(points.size()).arg(probes.size()));
        } else {
            auto initial=metrology::circleThroughThreePoints(cvp(clicks[0]),cvp(clicks[1]),cvp(clicks[2]));auto center=initial.center;double radius=initial.radius;if(radius<s.halfLength+2)throw std::runtime_error("Raggio troppo piccolo rispetto alla ROI.");
            std::vector<cv::Point2d> all;
            for(int i=0;i<number->value();++i){double a=2*CV_PI*i/number->value();cv::Point2d d(std::cos(a),std::sin(a));sample({center+d*radius,d*(direction->currentIndex()?-1.0:1.0)},all);}
            auto cov=metrology::coverage(found);
            QString coverageText=QString("Edge: %1/%2 (%3%)\nIntervallo massimo tra edge: %4 gradi").arg(cov.valid).arg(cov.total).arg(cov.percent,0,'f',1).arg(cov.maxGapDegrees,0,'f',2);
            if(all.size()<size_t(std::ceil(number->value()*0.8)))throw std::runtime_error((QString("Copertura insufficiente (minimo 80%).\n")+coverageText).toStdString());
            auto fitted=metrology::fitCircle(all);
            auto minimum=metrology::minimumZone(all,fitted);
                        circle=fitted;zone=minimum;
            circumscribed=metrology::minimumEnclosingCircle(all);
            QString micMessage;
            if(cov.maxGapDegrees>5.0+1e-9) micMessage="MIC e differenza non disponibili: intervallo edge > 5 gradi.";
            else {try{inscribed=metrology::maximumInscribedCircle(all);}catch(const std::exception &e){micMessage=e.what();}}
            double scale=pixelsPerMm>0?pixelsPerMm:1, errorScale=pixelsPerMm>0?1000/pixelsPerMm:1;
            QString unit=pixelsPerMm>0?"mm":"px",errorUnit=pixelsPerMm>0?QString::fromUtf8("\xc2\xb5m"):"px";
            result->setText(QString("Diametro LSC: %1 %2\nRotondita MZC (numerica): %3 %6\nPicco-valle LSC: %4 %6\nRMS LSC: %5 %6\n").arg(2*circle->radius/scale,0,'f',5).arg(unit).arg(zone->width()*errorScale,0,'f',4).arg(circle->peakToValley*errorScale,0,'f',4).arg(circle->rms*errorScale,0,'f',4).arg(errorUnit)+QString("Diametro MCC: %1 %2\n").arg(2*circumscribed->radius/scale,0,'f',5).arg(unit)
                +(inscribed?QString("Diametro MIC: %1 %2\nDifferenza radiale MCC-MIC: %3 %4\n").arg(2*inscribed->radius/scale,0,'f',5).arg(unit).arg((circumscribed->radius-inscribed->radius)*errorScale,0,'f',4).arg(errorUnit):micMessage+"\n")
                +coverageText+"\nMagenta: LSC | Arancio: MZC\nCiano: MIC | Rosso: MCC\nMCC-MIC: indicatore distinto dalla MZC.");
            if(filterEnabled->isChecked()){
                FilteredMeasurement measurement;
                measurement.settings={filterCutoff->value(),filterTension->value(),filterGap->value()};
                measurement.profile=metrology::filterRadialProfile(all,fitted.center,number->value(),measurement.settings);
                measurement.circle=metrology::fitCircle(measurement.profile.points);
                measurement.zone=metrology::minimumZone(measurement.profile.points,measurement.circle);
                measurement.circumscribed=metrology::minimumEnclosingCircle(measurement.profile.points);
                QString filteredMicMessage="MIC non disponibile: intervallo edge > 5 gradi.";
                if(cov.maxGapDegrees<=5.0+1e-9 && measurement.profile.maxGapDegrees<=5.0+1e-9){
                    try{measurement.inscribed=metrology::maximumInscribedCircle(measurement.profile.points);}
                    catch(const std::exception &error){filteredMicMessage=error.what();}
                }
                filtered=std::move(measurement);
                result->setText("Profilo acquisito\n"+result->text()+
                    QString("\n\nProfilo filtrato spline\nTaglio: %1 UPR | beta: %2\nCampioni ricampionati: %3\n"
                            "Diametro LSC: %4 %5\nRotondita MZC: %6 %7\nPicco-valle LSC: %8 %7\nRMS LSC: %9 %7\n")
                    .arg(filtered->settings.cutoffUpr).arg(filtered->settings.tension).arg(filtered->profile.points.size())
                    .arg(2*filtered->circle.radius/scale,0,'f',5).arg(unit)
                    .arg(filtered->zone.width()*errorScale,0,'f',4).arg(errorUnit)
                    .arg(filtered->circle.peakToValley*errorScale,0,'f',4).arg(filtered->circle.rms*errorScale,0,'f',4)+
                    QString("Diametro MCC: %1 %2\n").arg(2*filtered->circumscribed.radius/scale,0,'f',5).arg(unit)+
                    (filtered->inscribed?QString("Diametro MIC: %1 %2\nDifferenza radiale MCC-MIC: %3 %4")
                        .arg(2*filtered->inscribed->radius/scale,0,'f',5).arg(unit)
                        .arg((filtered->circumscribed.radius-filtered->inscribed->radius)*errorScale,0,'f',4).arg(errorUnit)
                        :filteredMicMessage));
            }
        }
    }catch(const std::exception &e){circle.reset();zone.reset();inscribed.reset();circumscribed.reset();filtered.reset();result->setText(QString("Misura non valida: %1").arg(e.what()));}
    redraw();
}
void MainWindow::redraw() {
    auto plot=static_cast<ResidualPlot*>(residualPlot);plot->samples.clear();plot->filteredSamples.clear();plot->unit=pixelsPerMm>0?QString::fromUtf8("\xc2\xb5m"):"px";
    if(circle){double scale=pixelsPerMm>0?1000/pixelsPerMm:1;for(size_t i=0;i<points.size();++i){auto d=points[i]-circle->center;double angle=std::atan2(d.y,d.x)*180/CV_PI;if(angle<0)angle+=360;plot->samples.push_back({angle,circle->residuals[i]*scale});}}
    if(filtered && circle){
        double scale=pixelsPerMm>0?1000/pixelsPerMm:1;
        for(size_t i=0;i<filtered->profile.points.size();++i){
            double radius=cv::norm(filtered->profile.points[i]-filtered->profile.center);
            plot->filteredSamples.push_back({360.0*i/filtered->profile.points.size(),(radius-circle->radius)*scale});
        }
    }
    plot->update();
    scene->clear();if(image.isNull())return;scene->addPixmap(QPixmap::fromImage(image));scene->setSceneRect(image.rect());
    auto pen=[](QColor c){QPen p(c);p.setCosmetic(true);return p;};
    for(int i=0;i<clicks.size();++i){auto p=clicks[i];scene->addEllipse(p.x()-3,p.y()-3,6,6,pen(Qt::yellow));if(mode==1 && i%2)scene->addLine(QLineF(clicks[i-1],p),pen(Qt::yellow));}
    if(mode==2 && clicks.size()==3){try{auto c=metrology::circleThroughThreePoints({clicks[0].x(),clicks[0].y()},{clicks[1].x(),clicks[1].y()},{clicks[2].x(),clicks[2].y()});scene->addEllipse(c.center.x-c.radius,c.center.y-c.radius,2*c.radius,2*c.radius,pen(Qt::yellow));}catch(const std::exception &e){result->setText(e.what());}}
    auto s=settings();for(size_t i=0;i<probes.size();++i){auto p=probes[i];auto d=p.direction;cv::Point2d n(-d.y,d.x);QPolygonF poly;for(auto v:{p.center-d*double(s.halfLength)-n*double(s.halfWidth),p.center+d*double(s.halfLength)-n*double(s.halfWidth),p.center+d*double(s.halfLength)+n*double(s.halfWidth),p.center-d*double(s.halfLength)+n*double(s.halfWidth)})poly<<QPointF(v.x,v.y);auto color=found[i]?QColor(40,160,220,130):QColor(Qt::red);scene->addPolygon(poly,pen(color));auto a=p.center-d*double(s.halfLength),b=p.center+d*double(s.halfLength);scene->addLine(a.x,a.y,b.x,b.y,pen(color));for(int sign:{-1,1}){auto q=b-d*4.0+n*(sign*2.0);scene->addLine(b.x,b.y,q.x,q.y,pen(color));}}
    for(auto p:points)scene->addEllipse(p.x-1.5,p.y-1.5,3,3,pen(Qt::green),QBrush(Qt::green));
    if(filtered){
        QPolygonF contour;
        for(auto point:filtered->profile.points)contour<<QPointF(point.x,point.y);
        scene->addPolygon(contour,pen(QColor("#a020c0")));
    }
    if(zone){for(double r:{zone->innerRadius,zone->outerRadius})scene->addEllipse(zone->center.x-r,zone->center.y-r,2*r,2*r,pen(QColor("#ff9900")));}
    auto boundary=[&](const std::optional<metrology::Circle>& circle,QColor color){if(!circle)return;auto c=*circle;auto p=pen(color);p.setStyle(Qt::DashLine);scene->addEllipse(c.center.x-c.radius,c.center.y-c.radius,2*c.radius,2*c.radius,p);scene->addLine(c.center.x-4,c.center.y,c.center.x+4,c.center.y,pen(color));scene->addLine(c.center.x,c.center.y-4,c.center.x,c.center.y+4,pen(color));};
    boundary(inscribed,Qt::cyan);boundary(circumscribed,Qt::red);
    if(circle){auto c=*circle;scene->addEllipse(c.center.x-c.radius,c.center.y-c.radius,2*c.radius,2*c.radius,pen(Qt::magenta));}
}
void MainWindow::exportCsv(){
    if(!circle || !zone || !circumscribed){QMessageBox::information(this,"Esporta","Eseguire prima una misura valida della circonferenza.");return;}
    auto name=QFileDialog::getSaveFileName(this,"Esporta CSV",{},"CSV (*.csv)");if(name.isEmpty())return;QFile f(name);if(!f.open(QIODevice::WriteOnly|QIODevice::Text)){QMessageBox::warning(this,"CSV",f.errorString());return;}
    QTextStream out(&f);writeCsv(out);
    if(out.status()!=QTextStream::Ok)QMessageBox::warning(this,"CSV","Errore durante la scrittura.");
}
void MainWindow::writeCsv(QTextStream &out) const {
    auto cov=metrology::coverage(found);
    out.setLocale(QLocale::c());out.setRealNumberPrecision(12);
    out<<"probe_index,angle_lsc_deg,x_px,y_px,residual_lsc_px,radius_mzc_px,diameter_lsc_px,pv_lsc_px,rms_lsc_px,mzc_width_px,mzc_inner_radius_px,mzc_outer_radius_px,mzc_center_x_px,mzc_center_y_px,valid_edges,total_probes,coverage_percent,max_gap_deg,diameter_mcc_px,mcc_center_x_px,mcc_center_y_px,diameter_mic_px,mic_center_x_px,mic_center_y_px,delta_radius_mcc_mic_px";
    if(pixelsPerMm>0)out<<",x_mm,y_mm,residual_lsc_um,diameter_lsc_mm,pv_lsc_um,rms_lsc_um,mzc_width_um,diameter_mcc_mm,diameter_mic_mm,delta_radius_mcc_mic_um,radius_mzc_mm,radius_lsc_mm,mzc_inner_radius_mm,mzc_outer_radius_mm,radius_mcc_mm,radius_mic_mm";
    if(filtered)out<<",profile_type,sample_index,profile_angle_deg,profile_radius_px,resampled_input_radius_px,filter_type,filter_cutoff_upr,filter_tension,filter_max_gap_deg,filter_actual_gap_deg,filter_sample_count,filter_center_x_px,filter_center_y_px,pixels_per_mm";
    out<<"\n";
    auto writeRows=[&](const std::vector<cv::Point2d> &points, const std::vector<bool> &found,
                       const std::optional<metrology::Circle> &circle, const std::optional<metrology::MinimumZone> &zone,
                       const std::optional<metrology::Circle> &circumscribed, const std::optional<metrology::Circle> &inscribed,
                       bool isFiltered){
    size_t index=0;
    for(size_t probe=0;probe<found.size();++probe){
        if(!found[probe])continue;
        auto p=points[index],d=p-circle->center;double angle=std::atan2(d.y,d.x)*180/CV_PI;if(angle<0)angle+=360;
        if(!isFiltered)out<<qulonglong(probe);
        out<<","<<angle<<","<<p.x<<","<<p.y<<","<<circle->residuals[index]<<","<<cv::norm(p-zone->center)<<","<<2*circle->radius<<","<<circle->peakToValley<<","<<circle->rms<<","<<zone->width()<<","<<zone->innerRadius<<","<<zone->outerRadius<<","<<zone->center.x<<","<<zone->center.y<<","<<cov.valid<<","<<cov.total<<","<<cov.percent<<","<<cov.maxGapDegrees<<","<<2*circumscribed->radius<<","<<circumscribed->center.x<<","<<circumscribed->center.y;
        if(inscribed)out<<","<<2*inscribed->radius<<","<<inscribed->center.x<<","<<inscribed->center.y<<","<<circumscribed->radius-inscribed->radius;
        else out<<",,,,";
        if(pixelsPerMm>0){double factor=1000/pixelsPerMm;out<<","<<p.x/pixelsPerMm<<","<<p.y/pixelsPerMm<<","<<circle->residuals[index]*factor<<","<<2*circle->radius/pixelsPerMm<<","<<circle->peakToValley*factor<<","<<circle->rms*factor<<","<<zone->width()*factor<<","<<2*circumscribed->radius/pixelsPerMm;if(inscribed)out<<","<<2*inscribed->radius/pixelsPerMm<<","<<(circumscribed->radius-inscribed->radius)*factor;else out<<",,";}
        if(pixelsPerMm>0){
            out<<","<<cv::norm(p-zone->center)/pixelsPerMm
               <<","<<circle->radius/pixelsPerMm
               <<","<<zone->innerRadius/pixelsPerMm
               <<","<<zone->outerRadius/pixelsPerMm
               <<","<<circumscribed->radius/pixelsPerMm
               <<",";
            if(inscribed)out<<inscribed->radius/pixelsPerMm;
        }
        if(filtered){
            double profileAngle=std::atan2(p.y-filtered->profile.center.y,p.x-filtered->profile.center.x)*180/CV_PI;
            if(profileAngle<0)profileAngle+=360;
            out<<","<<(isFiltered?"filtered":"acquired")<<","<<qulonglong(index)<<","<<profileAngle
               <<","<<cv::norm(p-filtered->profile.center)<<",";
            if(isFiltered)out<<filtered->profile.inputRadii[index];
            out<<",periodic_spline_ISO16610-22_2015,"<<filtered->settings.cutoffUpr
               <<","<<filtered->settings.tension<<","<<filtered->settings.maxGapDegrees
               <<","<<filtered->profile.maxGapDegrees<<","<<qulonglong(filtered->profile.points.size())
               <<","<<filtered->profile.center.x<<","<<filtered->profile.center.y<<",";
            if(pixelsPerMm>0)out<<pixelsPerMm;
        }
        out<<"\n";++index;
    }
    };
    writeRows(points,found,circle,zone,circumscribed,inscribed,false);
    if(filtered){
        writeRows(filtered->profile.points,std::vector<bool>(filtered->profile.points.size(),true),
                  filtered->circle,filtered->zone,filtered->circumscribed,filtered->inscribed,true);
    }
}
void MainWindow::calibrationFile(bool save){
    if(save && pixelsPerMm<=0){QMessageBox::information(this,"Scala","Nessuna calibrazione da salvare.");return;}
    auto name=save?QFileDialog::getSaveFileName(this,"Salva calibrazione",{},"JSON (*.json)"):QFileDialog::getOpenFileName(this,"Carica calibrazione",{},"JSON (*.json)");if(name.isEmpty())return;QFile f(name);if(!f.open(save?QIODevice::WriteOnly:QIODevice::ReadOnly)){QMessageBox::warning(this,"Scala",f.errorString());return;}
    if(save){QJsonObject o{{"version",1},{"pixelsPerMm",pixelsPerMm},{"width",calibrationSize.width()},{"height",calibrationSize.height()}};if(f.write(QJsonDocument(o).toJson())<0)QMessageBox::warning(this,"Scala",f.errorString());}
    else{auto o=QJsonDocument::fromJson(f.readAll()).object();double v=o["pixelsPerMm"].toDouble();QSize size(o["width"].toInt(),o["height"].toInt());if(o["version"].toInt()!=1 || !std::isfinite(v) || v<=0 || size.width()<=0 || size.height()<=0 || (!image.isNull() && image.size()!=size)){QMessageBox::warning(this,"Scala","Calibrazione non valida o risoluzione diversa.");return;}pixelsPerMm=v;calibrationSize=size;scaleLabel->setText(QString("Scala caricata: %1 px/mm. Verificare ottica e piano.").arg(v,0,'f',6));start(0);}
}






