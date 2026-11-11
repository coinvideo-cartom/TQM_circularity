#include "metrology.h"
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <array>
#include <limits>
#include <queue>

namespace metrology {
std::vector<double> periodicSpline(const std::vector<double> &values, double cutoffUpr, double tension) {
    const int count=int(values.size());
    if(count<6 || !std::isfinite(cutoffUpr) || cutoffUpr<=0 || cutoffUpr>=count/2.0 ||
       !std::isfinite(tension) || tension<0 || tension>1)
        throw std::runtime_error("Spline: taglio UPR deve essere positivo e inferiore a N/2; tensione tra 0 e 1.");
    for(double value:values)
        if(!std::isfinite(value))throw std::runtime_error("Spline: campione non finito.");
    cv::Mat input(count,1,CV_64F), spectrum, output;
    for(int i=0;i<count;++i)input.at<double>(i)=values[i];
    cv::dft(input,spectrum,cv::DFT_COMPLEX_OUTPUT);
    // Diagonalization of the periodic P and Q matrices of ISO 16610-22:2015, 4.3.3.
    // P has eigenvalue 4*sin(pi*k/N)^2 and Q=P^2. Alpha fixes 50% at cutoff.
    const double cutoffSine=std::sin(CV_PI*cutoffUpr/count);
    for(int k=0;k<count;++k){
        const double ratio=std::sin(CV_PI*std::min(k,count-k)/count)/cutoffSine;
        const double squared=ratio*ratio;
        const double transmission=1/(1+tension*squared+(1-tension)*squared*squared);
        spectrum.at<cv::Vec2d>(k)*=transmission;
    }
    cv::dft(spectrum,output,cv::DFT_INVERSE|cv::DFT_REAL_OUTPUT|cv::DFT_SCALE);
    return std::vector<double>(output.ptr<double>(),output.ptr<double>()+count);
}

FilteredProfile filterRadialProfile(const std::vector<cv::Point2d> &points, cv::Point2d center,
                                    int sampleCount, const SplineSettings &settings) {
    if(points.size()<6 || sampleCount<6 || !std::isfinite(center.x) || !std::isfinite(center.y) ||
       !std::isfinite(settings.maxGapDegrees) || settings.maxGapDegrees<=0 || settings.maxGapDegrees>180)
        throw std::runtime_error("Spline: profilo o limite di interpolazione non valido.");
    std::vector<cv::Point2d> polar;
    for(auto point:points){
        const auto delta=point-center;
        const double radius=cv::norm(delta);
        if(!std::isfinite(radius) || radius<=0)throw std::runtime_error("Spline: raggio non valido.");
        double angle=std::atan2(delta.y,delta.x);
        if(angle<0)angle+=2*CV_PI;
        polar.push_back({angle,radius});
    }
    // Acquisition order must describe a single-valued radial contour.
    double winding=0;
    for(size_t i=0;i<polar.size();++i){
        double step=polar[(i+1)%polar.size()].x-polar[i].x;
        if(step<=0)step+=2*CV_PI;
        winding+=step;
    }
    if(std::abs(winding-2*CV_PI)>1e-7)
        throw std::runtime_error("Spline: contorno non ordinato radialmente rispetto al centro LSC.");
    std::sort(polar.begin(),polar.end(),[](auto a,auto b){return a.x<b.x;});
    FilteredProfile profile;
    profile.center=center;
    for(size_t i=0;i<polar.size();++i){
        double next=i+1<polar.size()?polar[i+1].x:polar[0].x+2*CV_PI;
        double gap=next-polar[i].x;
        if(gap<1e-10)throw std::runtime_error("Spline: angoli duplicati.");
        profile.maxGapDegrees=std::max(profile.maxGapDegrees,gap*180/CV_PI);
    }
    if(profile.maxGapDegrees>settings.maxGapDegrees+1e-9)
        throw std::runtime_error("Spline: intervallo angolare tra punti superiore al limite di interpolazione.");
    if(settings.cutoffUpr>=180/profile.maxGapDegrees)
        throw std::runtime_error("Spline: campionamento acquisito insufficiente per il taglio UPR scelto.");
    polar.insert(polar.begin(),{polar.back().x-2*CV_PI,polar.back().y});
    polar.push_back({polar[1].x+2*CV_PI,polar[1].y});
    size_t right=1;
    for(int i=0;i<sampleCount;++i){
        const double angle=2*CV_PI*i/sampleCount;
        while(polar[right].x<angle)++right;
        const auto a=polar[right-1], b=polar[right];
        const double weight=(angle-a.x)/(b.x-a.x);
        profile.inputRadii.push_back(a.y+weight*(b.y-a.y));
    }
    const auto radii=periodicSpline(profile.inputRadii,settings.cutoffUpr,settings.tension);
    for(int i=0;i<sampleCount;++i){
        if(!std::isfinite(radii[i]) || radii[i]<=0)throw std::runtime_error("Spline: raggio filtrato non valido.");
        const double angle=2*CV_PI*i/sampleCount;
        profile.points.push_back(center+cv::Point2d(std::cos(angle),std::sin(angle))*radii[i]);
    }
    return profile;
}

Circle minimumEnclosingCircle(const std::vector<cv::Point2d> &points) {
    if(points.size()<3) throw std::runtime_error("MCC: punti insufficienti.");
    cv::Point2d origin=points.front();double scale=0;
    for(auto p:points){if(!std::isfinite(p.x)||!std::isfinite(p.y))throw std::runtime_error("MCC: coordinate non finite.");scale=std::max(scale,cv::norm(p-origin));}
    if(scale<=0)throw std::runtime_error("MCC: punti coincidenti.");
    std::vector<cv::Point2f> normalized;for(auto p:points)normalized.push_back(cv::Point2f((p-origin)/scale));
    cv::Point2f center;float radius;cv::minEnclosingCircle(normalized,center,radius);
    Circle c;c.center=origin+cv::Point2d(center)*scale;
    // Recompute radius in double precision to enclose every original point.
    for(auto p:points)c.radius=std::max(c.radius,cv::norm(p-c.center));return c;
}
Circle maximumInscribedCircle(const std::vector<cv::Point2d> &contour,double tolerancePx) {
    if(contour.size()<3 || !std::isfinite(tolerancePx) || tolerancePx<=0)throw std::runtime_error("MIC: contorno o tolleranza non validi.");
    const auto origin=contour.front();std::vector<cv::Point2d> pts;
    double xmin=0,xmax=0,ymin=0,ymax=0,area=0;
    for(auto p:contour){if(!std::isfinite(p.x)||!std::isfinite(p.y))throw std::runtime_error("MIC: coordinate non finite.");p-=origin;pts.push_back(p);xmin=std::min(xmin,p.x);xmax=std::max(xmax,p.x);ymin=std::min(ymin,p.y);ymax=std::max(ymax,p.y);}
    const int n=int(pts.size());auto cross=[](cv::Point2d a,cv::Point2d b){return a.x*b.y-a.y*b.x;};
    double extent=std::max(xmax-xmin,ymax-ymin),eps=1e-12*extent*extent;
    for(int i=0;i<n;++i){auto a=pts[i],b=pts[(i+1)%n];if(cv::norm(b-a)<=1e-12*extent)throw std::runtime_error("MIC: vertici duplicati.");area+=cross(a,b);
        for(int j=i+1;j<n;++j){if(j==i+1 || (i==0 && j==n-1))continue;auto c=pts[j],d=pts[(j+1)%n];
            double u=cross(b-a,c-a),v=cross(b-a,d-a),w=cross(d-c,a-c),z=cross(d-c,b-c);
            if(std::max(std::min(a.x,b.x),std::min(c.x,d.x))<=std::min(std::max(a.x,b.x),std::max(c.x,d.x)) && std::max(std::min(a.y,b.y),std::min(c.y,d.y))<=std::min(std::max(a.y,b.y),std::max(c.y,d.y)) && ((u>=-eps&&v<=eps)||(u<=eps&&v>=-eps)) && ((w>=-eps&&z<=eps)||(w<=eps&&z>=-eps)))throw std::runtime_error("MIC: contorno auto-intersecante.");
        }
    }
    if(extent<=0 || std::abs(area)<=eps)throw std::runtime_error("MIC: contorno degenere.");
    auto distance=[&](cv::Point2d p){bool inside=false;double closest=std::numeric_limits<double>::infinity();
        for(int i=0,j=n-1;i<n;j=i++){auto a=pts[j],b=pts[i],d=b-a;double t=std::clamp((p-a).dot(d)/d.dot(d),0.0,1.0);closest=std::min(closest,cv::norm(p-(a+d*t)));
            if((a.y>p.y)!=(b.y>p.y) && p.x<(b.x-a.x)*(p.y-a.y)/(b.y-a.y)+a.x)inside=!inside;}
        return inside?closest:-closest;
    };
    struct Cell{cv::Point2d p;double h,d,upper;bool operator<(const Cell&b)const{return upper<b.upper;}};
    auto cell=[&](cv::Point2d p,double h){double d=distance(p);return Cell{p,h,d,d+h*std::sqrt(2.0)};};
    std::priority_queue<Cell> queue;auto initial=cell({(xmin+xmax)/2,(ymin+ymax)/2},extent/2);queue.push(initial);Cell best=cell(pts[0],0);if(initial.d>best.d)best=initial;
    // Signed distance is 1-Lipschitz: upper bounds certify the radius tolerance on this polygon.
    int processed=0;
    while(!queue.empty()) {auto current=queue.top();queue.pop();if(current.upper-best.d<=tolerancePx)continue;
        if(++processed>250000)throw std::runtime_error("MIC: limite di calcolo raggiunto, risultato non disponibile.");
        double h=current.h/2;for(int x:{-1,1})for(int y:{-1,1}){auto child=cell(current.p+cv::Point2d(x*h,y*h),h);if(child.d>best.d)best=child;if(child.upper-best.d>tolerancePx)queue.push(child);}
    }
    if(best.d<=0)throw std::runtime_error("MIC: interno del contorno non risolto.");
    Circle result;result.center=origin+best.p;result.radius=best.d;return result;
}
Coverage coverage(const std::vector<bool> &found) {
    Coverage c; c.total=int(found.size()); int first=-1,last=-1,maxGap=0;
    for(int i=0;i<c.total;++i) if(found[i]) {++c.valid;if(first<0)first=i;else maxGap=std::max(maxGap,i-last);last=i;}
    if(c.total) c.percent=100.0*c.valid/c.total;
    if(c.valid) {maxGap=std::max(maxGap,c.total-last+first);c.maxGapDegrees=360.0*maxGap/c.total;}
    return c;
}
MinimumZone minimumZone(const std::vector<cv::Point2d> &points,const Circle &lsc) {
    if(points.size()<6 || !std::isfinite(lsc.radius) || lsc.radius<=0) throw std::runtime_error("Dati insufficienti per zona minima.");
    // Normalize coordinates for stable tolerances across image scales/translations.
    std::vector<cv::Point2d> pts; for(auto p:points) {
        if(!std::isfinite(p.x) || !std::isfinite(p.y)) throw std::runtime_error("Coordinate non finite.");
        pts.push_back((p-lsc.center)/lsc.radius);
    }
    auto radii=[&](cv::Point2d center) {double lo=std::numeric_limits<double>::infinity(),hi=0;for(auto p:pts){double r=cv::norm(p-center);lo=std::min(lo,r);hi=std::max(hi,r);}return std::pair<double,double>(lo,hi);};
    auto objective=[&](cv::Point2d p){auto r=radii(p);return r.second-r.first;};
    struct Vertex {cv::Point2d p;double f;};
    auto vertex=[&](cv::Point2d p){return Vertex{p,objective(p)};};
    Vertex best=vertex({0,0}); bool converged=false;
    const double step=std::max(0.001,lsc.peakToValley/lsc.radius);
    // Multi-start Nelder-Mead on the actual (nonlinear) radial annulus width.
    for(int start=0;start<9;++start) {
        cv::Point2d origin;
        if(start) {double a=(start-1)*CV_PI/4;origin={step*std::cos(a),step*std::sin(a)};}
        std::array<Vertex,3> simplex={vertex(origin),vertex(origin+cv::Point2d(step,0)),vertex(origin+cv::Point2d(0,step))};
        bool done=false;
        for(int iteration=0;iteration<1000;++iteration) {
            std::sort(simplex.begin(),simplex.end(),[](const Vertex&a,const Vertex&b){return a.f<b.f;});
            if(std::max(cv::norm(simplex[1].p-simplex[0].p),cv::norm(simplex[2].p-simplex[0].p))<1e-10) {done=true;break;}
            auto mid=(simplex[0].p+simplex[1].p)*0.5;
            auto reflected=vertex(mid*2-simplex[2].p);
            if(reflected.f<simplex[0].f) {auto expanded=vertex(mid+(reflected.p-mid)*2);simplex[2]=expanded.f<reflected.f?expanded:reflected;}
            else if(reflected.f<simplex[1].f) simplex[2]=reflected;
            else {
                bool outside=reflected.f<simplex[2].f;
                auto contracted=vertex(mid+((outside?reflected.p:simplex[2].p)-mid)*0.5);
                if(contracted.f<(outside?reflected.f:simplex[2].f)) simplex[2]=contracted;
                else for(int i=1;i<3;++i) simplex[i]=vertex((simplex[i].p+simplex[0].p)*0.5);
            }
        }
        std::sort(simplex.begin(),simplex.end(),[](const Vertex&a,const Vertex&b){return a.f<b.f;});
        if(done && (!converged || simplex[0].f<best.f)) {best=simplex[0];converged=true;}
    }
    if(!converged) throw std::runtime_error("Ottimizzazione zona minima non convergente.");
    // The LSC annulus is always an available upper bound.
    if(objective({0,0})<best.f) best=vertex({0,0});
    auto r=radii(best.p);
    return {lsc.center+best.p*lsc.radius,r.first*lsc.radius,r.second*lsc.radius,converged};
}
Circle circleThroughThreePoints(cv::Point2d a, cv::Point2d b, cv::Point2d c) {
    const auto u=b-a, v=c-a;
    const double cross=u.x*v.y-u.y*v.x;
    const double scale=std::max({u.dot(u),v.dot(v),(c-b).dot(c-b)});
    if(!std::isfinite(scale) || scale<1e-12 || std::abs(cross)<=1e-6*scale)
        throw std::runtime_error("Selezionare tre punti distinti e non allineati sul bordo, ben distanziati tra loro.");
    Circle result;
    const cv::Point2d offset((u.dot(u)*v.y-v.dot(v)*u.y)/(2*cross),
                             (u.x*v.dot(v)-v.x*u.dot(u))/(2*cross));
    result.center=a+offset;
    result.radius=cv::norm(offset);
    return result;
}
std::optional<Edge> extract(const cv::Mat &im, const Probe &p, const Settings &s) {
    if(im.empty() || im.type()!=CV_8UC1 || s.sigma<0.4 || s.halfLength<4 || s.halfWidth<0 || s.threshold<=0 || cv::norm(p.direction)<1e-9) return {};
    auto d=p.direction/cv::norm(p.direction); cv::Point2d n(-d.y,d.x);
    const int count=2*s.halfLength+1, margin=int(std::ceil(3*s.sigma))+1;
    if(count<2*margin+3) return {};
    cv::Mat profile(1,count,CV_64F);
    for(int i=0;i<count;++i) {
        double sum=0;
        for(int j=-s.halfWidth;j<=s.halfWidth;++j) {
            auto q=p.center+d*double(i-s.halfLength)+n*double(j);
            if(q.x<0 || q.y<0 || q.x>=im.cols-1 || q.y>=im.rows-1) return {};
            int x=int(q.x),y=int(q.y); double fx=q.x-x,fy=q.y-y;
            sum+=(1-fy)*((1-fx)*im.at<uchar>(y,x)+fx*im.at<uchar>(y,x+1))+fy*((1-fx)*im.at<uchar>(y+1,x)+fx*im.at<uchar>(y+1,x+1));
        }
        profile.at<double>(0,i)=sum/(2*s.halfWidth+1);
    }
    cv::Mat smooth; cv::GaussianBlur(profile,smooth,cv::Size(2*(margin-1)+1,1),s.sigma,0,cv::BORDER_REFLECT_101);
    std::vector<double> g(count,0);
    for(int i=1;i<count-1;++i) g[i]=(smooth.at<double>(0,i+1)-smooth.at<double>(0,i-1))*0.5;
    std::optional<Edge> best;
    for(int i=margin;i<count-margin;++i) {
        double a=std::abs(g[i]), l=std::abs(g[i-1]), r=std::abs(g[i+1]);
        if(a<s.threshold || a<l || a<=r || (s.polarity>0 && g[i]<=0) || (s.polarity<0 && g[i]>=0)) continue;
        double den=l-2*a+r, delta=std::abs(den)>1e-12 ? 0.5*(l-r)/den : 0;
        Edge e{p.center+d*(i-s.halfLength+std::clamp(delta,-0.5,0.5)),g[i]};
        if(!best || s.selection==2 || (s.selection==0 && a>std::abs(best->amplitude))) best=e;
        if(s.selection==1) break;
    }
    return best;
}
Circle fitCircle(const std::vector<cv::Point2d> &pts) {
    if(pts.size()<6) throw std::runtime_error("Servono almeno 6 punti validi.");
    cv::Point2d mean; for(auto p:pts) mean+=p; mean*=1.0/pts.size();
    cv::Mat a(int(pts.size()),3,CV_64F),b(int(pts.size()),1,CV_64F),x;
    for(int i=0;i<int(pts.size());++i) { auto p=pts[i]-mean; a.at<double>(i,0)=2*p.x; a.at<double>(i,1)=2*p.y; a.at<double>(i,2)=1; b.at<double>(i)=p.dot(p); }
    cv::SVD svd(a); if(svd.w.at<double>(2)<1e-8*svd.w.at<double>(0)) throw std::runtime_error("Punti degeneri: impossibile stimare il cerchio.");
    cv::solve(a,b,x,cv::DECOMP_SVD); Circle c; c.center=mean+cv::Point2d(x.at<double>(0),x.at<double>(1));
    for(auto p:pts) c.radius+=cv::norm(p-c.center); c.radius/=pts.size();
    // Geometric least-squares refinement of center and radius.
    for(int k=0;k<30;++k) {
        for(int i=0;i<int(pts.size());++i) { auto d=c.center-pts[i]; double r=cv::norm(d); if(r<1e-9) throw std::runtime_error("Fit degenere."); a.at<double>(i,0)=d.x/r; a.at<double>(i,1)=d.y/r; a.at<double>(i,2)=-1; b.at<double>(i)=c.radius-r; }
        cv::solve(a,b,x,cv::DECOMP_SVD); c.center+=cv::Point2d(x.at<double>(0),x.at<double>(1)); c.radius+=x.at<double>(2); if(cv::norm(x)<1e-9) break;
    }
    for(auto p:pts) { double r=cv::norm(p-c.center)-c.radius; c.residuals.push_back(r); c.rms+=r*r; }
    auto mm=std::minmax_element(c.residuals.begin(),c.residuals.end()); c.peakToValley=*mm.second-*mm.first; c.rms=std::sqrt(c.rms/pts.size()); return c;
}
Calibration calibrate(const std::vector<cv::Point2d> &a,const std::vector<cv::Point2d> &b,double mm) {
    if(a.size()<6 || b.size()<6 || !std::isfinite(mm) || mm<=0) throw std::runtime_error("Calibrazione: servono almeno 6 edge per lato e una lunghezza positiva.");
    auto line=[](const std::vector<cv::Point2d>& p) { cv::Vec4f l; cv::fitLine(p,l,cv::DIST_L2,0,0.01,0.01); return l; };
    auto la=line(a),lb=line(b); cv::Point2d da(la[0],la[1]),db(lb[0],lb[1]);
    if(da.dot(db)<0) db=-db;
    Calibration c; c.angleDegrees=std::acos(std::clamp(da.dot(db),-1.0,1.0))*180/CV_PI;
    if(c.angleDegrees>2) throw std::runtime_error("Lati non paralleli (oltre 2 gradi): controllare selezione e prospettiva.");
    auto d=(da+db)/cv::norm(da+db); cv::Point2d n(-d.y,d.x); double ma=0,mb=0;
    for(auto p:a) ma+=p.dot(n); for(auto p:b) mb+=p.dot(n); ma/=a.size(); mb/=b.size();
    c.distance=std::abs(mb-ma); if(c.distance<1) throw std::runtime_error("Distanza tra lati insufficiente.");
    for(auto p:a) c.rms+=std::pow(p.dot(n)-ma,2); for(auto p:b) c.rms+=std::pow(p.dot(n)-mb,2);
    c.rms=std::sqrt(c.rms/(a.size()+b.size())); c.pixelsPerMm=c.distance/mm; return c;
}
}
