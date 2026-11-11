#include "metrology.h"
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <stdexcept>
#include <cmath>
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){try{
    for(int count:{360,721,7200}){
        for(double tension:{0.0,0.6252,1.0}){
            std::vector<double> signal(count);
            for(int i=0;i<count;++i)signal[i]=100+2*std::cos(2*CV_PI*15*i/count+0.37);
            auto filtered=metrology::periodicSpline(signal,15,tension);
            for(int i=0;i<count;++i)
                check(std::abs(filtered[i]-(100+std::cos(2*CV_PI*15*i/count+0.37)))<1e-10,"Spline 50% cutoff and phase");
            // Check the defining cyclic matrix equation directly on a mixed signal.
            for(int i=0;i<count;++i)signal[i]+=0.3*std::sin(2*CV_PI*71*i/count);
            filtered=metrology::periodicSpline(signal,15,tension);
            const double alpha=1/(2*std::sin(CV_PI*15/count));
            auto sample=[&](int i){return filtered[(i+count)%count];};
            for(int i=0;i<count;++i){
                double second=2*sample(i)-sample(i-1)-sample(i+1);
                double fourth=6*sample(i)-4*(sample(i-1)+sample(i+1))+sample(i-2)+sample(i+2);
                double reconstructed=sample(i)+tension*alpha*alpha*second+(1-tension)*std::pow(alpha,4)*fourth;
                check(std::abs(reconstructed-signal[i])<2e-5,"Spline cyclic matrix residual");
            }
        }
    }
    std::vector<cv::Point2d> radial;
    for(int i=0;i<360;++i){
        double angle=2*CV_PI*i/360,radius=100+2*std::cos(15*angle);
        radial.push_back(cv::Point2d(123,456)+cv::Point2d(std::cos(angle),std::sin(angle))*radius);
    }
    auto profile=metrology::filterRadialProfile(radial,{123,456},360,{15,0,5});
    for(int i=0;i<360;++i)
        check(std::abs(cv::norm(profile.points[i]-profile.center)-(100+std::cos(2*CV_PI*15*i/360)))<1e-9,"Spline radial conversion");
    radial.erase(radial.begin());
    profile=metrology::filterRadialProfile(radial,{123,456},360,{15,0,5});
    check(std::abs(profile.maxGapDegrees-2)<1e-9 && profile.points.size()==360,"Spline seam interpolation");
    bool gapRejected=false;
    try{metrology::filterRadialProfile(radial,{123,456},360,{15,0,1.5});}catch(...){gapRejected=true;}
    check(gapRejected,"Spline excessive gap accepted");
    for(auto settings:std::vector<metrology::SplineSettings>{{180,0,5},{0,0,5},{15,-1,5},{15,1.1,5}}){
        bool invalid=false;try{metrology::periodicSpline(std::vector<double>(360,100),settings.cutoffUpr,settings.tension);}catch(...){invalid=true;}
        check(invalid,"Spline invalid parameters accepted");
    }
    auto seed=metrology::circleThroughThreePoints({110,20},{10,120},{-90,20});
    check(cv::norm(seed.center-cv::Point2d(10,20))<1e-9 && std::abs(seed.radius-100)<1e-9,"three point circle");
    auto reversed=metrology::circleThroughThreePoints({-90,20},{10,120},{110,20});
    check(cv::norm(reversed.center-seed.center)<1e-9,"three point order");
    for(auto third:{cv::Point2d(2,2),cv::Point2d(1,1)}) {
        bool invalid=false;try{metrology::circleThroughThreePoints({0,0},{1,1},third);}catch(...){invalid=true;}
        check(invalid,"degenerate three point circle accepted");
    }
    metrology::Settings s;cv::Mat im(256,256,CV_8U);
    for(int y=0;y<im.rows;++y)for(int x=0;x<im.cols;++x)im.at<uchar>(y,x)=cv::saturate_cast<uchar>(30+200*0.5*(1+std::erf((x-120.35)/1.4)));
    auto e=metrology::extract(im,{{120,120},{1,0}},s);check(bool(e),"edge missing");check(std::abs(e->point.x-120.35)<0.12,"subpixel edge accuracy");
    s.polarity=-1;check(!metrology::extract(im,{{120,120},{1,0}},s),"wrong polarity accepted");check(bool(metrology::extract(im,{{120,120},{-1,0}},s)),"reverse direction");
    s.polarity=0;check(!metrology::extract(im,{{1,1},{1,0}},s),"out of bounds accepted");check(!metrology::extract(cv::Mat(100,100,CV_8U,cv::Scalar(50)),{{50,50},{1,0}},s),"flat edge accepted");
    std::vector<cv::Point2d> a,b;cv::Point2d t(std::cos(0.4),std::sin(0.4)),n(-t.y,t.x);
    for(int i=0;i<20;++i){a.push_back(cv::Point2d(200,100)+t*double(i*3));b.push_back(a.back()+n*100.0);}
    auto cal=metrology::calibrate(a,b,10);check(std::abs(cal.pixelsPerMm-10)<1e-5,"rotated calibration");
    b.clear();for(int i=0;i<20;++i)b.push_back(cv::Point2d(i*3,100));bool rejected=false;try{metrology::calibrate(a,b,10);}catch(...){rejected=true;}check(rejected,"nonparallel sides accepted");
    cv::Mat disk(400,400,CV_8U);for(int y=0;y<disk.rows;++y)for(int x=0;x<disk.cols;++x){double r=std::hypot(x-200.3,y-199.7);disk.at<uchar>(y,x)=cv::saturate_cast<uchar>(30+200*0.5*(1+std::erf((r-100.2)/1.4)));}
    std::vector<cv::Point2d> p;for(int i=0;i<180;++i){double angle=2*CV_PI*i/180;cv::Point2d d(std::cos(angle),std::sin(angle));auto hit=metrology::extract(disk,{cv::Point2d(200,200)+d*100,d},s);check(bool(hit),"radial edge missing");p.push_back(hit->point);}
    auto c=metrology::fitCircle(p);check(cv::norm(c.center-cv::Point2d(200.3,199.7))<0.05,"circle center");check(std::abs(c.radius-100.2)<0.12,"circle radius");check(c.peakToValley<0.15,"circle roundness");
    p.clear();for(int i=0;i<360;++i){double angle=2*CV_PI*i/360,r=100+0.7*std::cos(3*angle);p.push_back({20+r*std::cos(angle),30+r*std::sin(angle)});}c=metrology::fitCircle(p);check(std::abs(c.peakToValley-1.4)<1e-5,"lobed roundness");
    auto mz=metrology::minimumZone(p,c);check(mz.converged && std::abs(mz.width()-1.4)<1e-6,"MZ trilobed profile");
    // Unequal angular density moves the LSC center; balanced limiting contacts still fix MZ at the origin.
    p={{99,0},{-99,0},{0,101},{0,-101},{100,0},{0,100},{-100,0},{0,-100}};
    for(int i=0;i<60;++i){double angle=i*0.001;p.push_back({100.8*std::cos(angle),100.8*std::sin(angle)});}
    c=metrology::fitCircle(p);mz=metrology::minimumZone(p,c);
    check(std::abs(mz.width()-2)<1e-6 && cv::norm(mz.center)<1e-5,"known minimum annulus");
    check(mz.width()<c.peakToValley-0.01,"MZ must improve biased LSC");
    for(auto q:p){double r=cv::norm(q-mz.center);check(r>=mz.innerRadius-1e-8 && r<=mz.outerRadius+1e-8,"MZ enclosure");}
    for(auto &q:p)q=q*3.7+cv::Point2d(100000,-250000);
    c=metrology::fitCircle(p);mz=metrology::minimumZone(p,c);check(std::abs(mz.width()-7.4)<1e-5,"MZ scale translation invariance");
    p.clear();for(int i=0;i<360;++i){double a=i*CV_PI/180;p.push_back({4+80*std::cos(a),-8+80*std::sin(a)});}
    c=metrology::fitCircle(p);mz=metrology::minimumZone(p,c);check(mz.width()<1e-7,"MZ perfect circle");
    auto mic=metrology::maximumInscribedCircle(p),mcc=metrology::minimumEnclosingCircle(p);
    check(std::abs(mic.radius-80*std::cos(CV_PI/360))<0.0011,"MIC polygon chord radius");
    check(std::abs(mcc.radius-80)<0.0001,"MCC circle radius");
    std::vector<cv::Point2d> square={{0,0},{10,0},{10,10},{0,10}};
    mic=metrology::maximumInscribedCircle(square);mcc=metrology::minimumEnclosingCircle(square);
    check(std::abs(mic.radius-5)<0.0011 && cv::norm(mic.center-cv::Point2d(5,5))<0.002,"MIC square");
    check(std::abs(mcc.radius-std::sqrt(50.0))<1e-5,"MCC square");
    std::vector<cv::Point2d> triangle={{0,0},{6,0},{0,8}};
    mic=metrology::maximumInscribedCircle(triangle);mcc=metrology::minimumEnclosingCircle(triangle);
    check(std::abs(mic.radius-2)<0.0011 && cv::norm(mic.center-cv::Point2d(2,2))<0.005,"MIC triangle");
    check(std::abs(mcc.radius-5)<1e-5 && cv::norm(mcc.center-cv::Point2d(3,4))<1e-5,"MCC triangle");
    check(std::abs((mcc.radius-mic.radius)-3)<0.0011,"MCC-MIC different centers");
    std::reverse(triangle.begin(),triangle.end());for(auto &q:triangle)q+=cv::Point2d(1000000,-2000000);
    mic=metrology::maximumInscribedCircle(triangle);check(std::abs(mic.radius-2)<0.0011,"MIC reverse translated contour");
    std::vector<cv::Point2d> concave={{0,0},{6,0},{6,2},{2,2},{2,6},{0,6}};
    mic=metrology::maximumInscribedCircle(concave);check(std::abs(mic.radius-(4-2*std::sqrt(2.0)))<0.0011,"MIC concave polygon");
    for(auto invalid:std::vector<std::vector<cv::Point2d>>{{{0,0},{2,2},{0,2},{2,0}},{{0,0},{1,0},{2,0}},{{0,0},{1,0},{1,0},{0,1}}}){bool failed=false;try{metrology::maximumInscribedCircle(invalid);}catch(...){failed=true;}check(failed,"MIC invalid contour accepted");}
    auto coverage=metrology::coverage({false,true,true,true,true,false});check(coverage.valid==4 && std::abs(coverage.maxGapDegrees-180)<1e-9,"coverage wrap gap");
    check(metrology::coverage({true,true,true,true}).maxGapDegrees==90,"full coverage sampling gap");
    check(metrology::coverage({false,false}).percent==0 && metrology::coverage({false,false}).maxGapDegrees==360,"missing coverage");
    p.clear();for(int i=0;i<20;++i)p.push_back({double(i),2.0*i});rejected=false;try{metrology::fitCircle(p);}catch(...){rejected=true;}check(rejected,"collinear accepted");
    std::cout<<"PASS: subpixel, polarity, direction, bounds, flat image, rotated calibration, parallelism, radial extraction, circle fit, lobing, degeneracy\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
