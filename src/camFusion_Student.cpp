
#include <iostream>
#include <algorithm>
#include <numeric>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include "camFusion.hpp"
#include "dataStructures.h"

#include "matching2D.hpp"
#include <map>

using namespace std;


// Create groups of Lidar points whose projection into the camera falls into the same bounding box
void clusterLidarWithROI(std::vector<BoundingBox> &boundingBoxes, std::vector<LidarPoint> &lidarPoints, float shrinkFactor, cv::Mat &P_rect_xx, cv::Mat &R_rect_xx, cv::Mat &RT)
{
    // loop over all Lidar points and associate them to a 2D bounding box
    cv::Mat X(4, 1, cv::DataType<double>::type);
    cv::Mat Y(3, 1, cv::DataType<double>::type);
    
    for (auto it1 = lidarPoints.begin(); it1 != lidarPoints.end(); ++it1)
    {
        // assemble vector for matrix-vector-multiplication
        X.at<double>(0, 0) = it1->x;
        X.at<double>(1, 0) = it1->y;
        X.at<double>(2, 0) = it1->z;
        X.at<double>(3, 0) = 1;
        
        // project Lidar point into camera
        Y = P_rect_xx * R_rect_xx * RT * X;
        cv::Point pt;
        pt.x = Y.at<double>(0, 0) / Y.at<double>(0, 2); // pixel coordinates
        pt.y = Y.at<double>(1, 0) / Y.at<double>(0, 2);
        
        vector<vector<BoundingBox>::iterator> enclosingBoxes; // pointers to all bounding boxes which enclose the current Lidar point
        for (vector<BoundingBox>::iterator it2 = boundingBoxes.begin(); it2 != boundingBoxes.end(); ++it2)
        {
            // shrink current bounding box slightly to avoid having too many outlier points around the edges
            cv::Rect smallerBox;
            smallerBox.x = (*it2).roi.x + shrinkFactor * (*it2).roi.width / 2.0;
            smallerBox.y = (*it2).roi.y + shrinkFactor * (*it2).roi.height / 2.0;
            smallerBox.width = (*it2).roi.width * (1 - shrinkFactor);
            smallerBox.height = (*it2).roi.height * (1 - shrinkFactor);
            
            // check wether point is within current bounding box
            if (smallerBox.contains(pt))
            {
                enclosingBoxes.push_back(it2);
            }
            
        } // eof loop over all bounding boxes
        
        // check wether point has been enclosed by one or by multiple boxes
        if (enclosingBoxes.size() == 1)
        {
            // add Lidar point to bounding box
            enclosingBoxes[0]->lidarPoints.push_back(*it1);
        }
        
    } // eof loop over all Lidar points
}


void show3DObjects(std::vector<BoundingBox> &boundingBoxes, cv::Size worldSize, cv::Size imageSize, bool bWait)
{
    // create topview image
    cv::Mat topviewImg(imageSize, CV_8UC3, cv::Scalar(255, 255, 255));
    
    for(auto it1=boundingBoxes.begin(); it1!=boundingBoxes.end(); ++it1)
    {
        // create randomized color for current 3D object
        cv::RNG rng(it1->boxID);
        cv::Scalar currColor = cv::Scalar(rng.uniform(0,150), rng.uniform(0, 150), rng.uniform(0, 150));
        
        // plot Lidar points into top view image
        int top=1e8, left=1e8, bottom=0.0, right=0.0;
        float xwmin=1e8, ywmin=1e8, ywmax=-1e8;
        for (auto it2 = it1->lidarPoints.begin(); it2 != it1->lidarPoints.end(); ++it2)
        {
            // world coordinates
            float xw = (*it2).x; // world position in m with x facing forward from sensor
            float yw = (*it2).y; // world position in m with y facing left from sensor
            xwmin = xwmin<xw ? xwmin : xw;
            ywmin = ywmin<yw ? ywmin : yw;
            ywmax = ywmax>yw ? ywmax : yw;
            
            // top-view coordinates
            int y = (-xw * imageSize.height / worldSize.height) + imageSize.height;
            int x = (-yw * imageSize.width / worldSize.width) + imageSize.width / 2;
            
            // find enclosing rectangle
            top = top<y ? top : y;
            left = left<x ? left : x;
            bottom = bottom>y ? bottom : y;
            right = right>x ? right : x;
            
            // draw individual point
            cv::circle(topviewImg, cv::Point(x, y), 4, currColor, -1);
        }
        
        // draw enclosing rectangle
        cv::rectangle(topviewImg, cv::Point(left, top), cv::Point(right, bottom),cv::Scalar(0,0,0), 2);
        
        // augment object with some key data
        char str1[200], str2[200];
        sprintf(str1, "id=%d, #pts=%d", it1->boxID, (int)it1->lidarPoints.size());
        putText(topviewImg, str1, cv::Point2f(left-250, bottom+50), cv::FONT_ITALIC, 2, currColor);
        sprintf(str2, "xmin=%2.2f m, yw=%2.2f m", xwmin, ywmax-ywmin);
        putText(topviewImg, str2, cv::Point2f(left-250, bottom+125), cv::FONT_ITALIC, 2, currColor);
    }
    
    // plot distance markers
    float lineSpacing = 2.0; // gap between distance markers
    int nMarkers = floor(worldSize.height / lineSpacing);
    for (size_t i = 0; i < nMarkers; ++i)
    {
        int y = (-(i * lineSpacing) * imageSize.height / worldSize.height) + imageSize.height;
        cv::line(topviewImg, cv::Point(0, y), cv::Point(imageSize.width, y), cv::Scalar(255, 0, 0));
    }
    
    // display image
    string windowName = "3D Objects";
    cv::namedWindow(windowName, 1);
    cv::imshow(windowName, topviewImg);
    
    if(bWait)
    {
        cv::waitKey(0); // wait for key to be pressed
    }
}


// associate a given bounding box with the keypoints it contains
void clusterKptMatchesWithROI(BoundingBox &boundingBox, std::vector<cv::KeyPoint> &kptsPrev, std::vector<cv::KeyPoint> &kptsCurr, std::vector<cv::DMatch> &kptMatches)
{
    // ...FP.3 : Associate Keypoint Correspondences with Bounding Boxes Before a TTC estimate can be computed in the next exercise, you need to find all keypoint matches that belong to each 3D object. You can do this by simply checking wether the corresponding keypoints are within the region of interest in the camera image. All matches which satisfy this condition should be added to a vector. The problem you will find is that there will be outliers among your matches. To eliminate those, I recommend that you compute a robust mean of all the euclidean distances between keypoint matches and then remove those that are too far away from the mean.
    //The task is complete once the code performs as described and adds the keypoint correspondences to the "kptMatches" property of the respective bounding boxes. Also, outlier matches have been removed based on the euclidean distance between them in relation to all the matches in the bounding box.
    
    // Loop over all matches in the current frame
   //for test
    for (cv::DMatch match : kptMatches) {
        if (boundingBox.roi.contains(kptsCurr[match.trainIdx].pt)) {
            boundingBox.kptMatches.push_back(match);
        }
    }
    /*
    // loop the kptmatches and find the kpts pre and cuur in bounding box, and calculate the means distance
    float dis_sum = 0;
    int nr_dis = 0;
    float avg_sum  = 0;
    std::vector<cv::DMatch> kpt_box_match;
    for(auto it_kps = kptMatches.begin(); it_kps !=kptMatches.end(); ++it_kps)
    {
        bool b_x_inbox = kptsCurr[it_kps->trainIdx].pt.x>boundingBox.roi.x &&kptsCurr[it_kps->trainIdx].pt.x<boundingBox.roi.x+boundingBox.roi.width;
        bool b_y_inbox = kptsCurr[it_kps->trainIdx].pt.y>boundingBox.roi.y &&kptsCurr[it_kps->trainIdx].pt.y<boundingBox.roi.y+boundingBox.roi.height;
        if(b_x_inbox && b_y_inbox)
        {
            ++nr_dis;
            dis_sum += it_kps->distance;
            kpt_box_match.push_back(*it_kps);
        }
    }
    if(nr_dis != 0)
    {
        avg_sum = dis_sum/nr_dis;
    }
    
    // loop the matched one in box and remove the outliers
    for(auto it_kps = kpt_box_match.begin(); it_kps !=kpt_box_match.end(); ++it_kps)
    {
        if(it_kps->distance<avg_sum*1.1 && it_kps->distance>avg_sum*0.9)
        {
            //cout<< "debug distance " << it_kps->distance <<endl;
            boundingBox.kptMatches.push_back(*it_kps);
        }
    }
     */
}


// Compute time-to-collision (TTC) based on keypoint correspondences in successive images
void computeTTCCamera(std::vector<cv::KeyPoint> &kptsPrev, std::vector<cv::KeyPoint> &kptsCurr, 
                      std::vector<cv::DMatch> kptMatches, double frameRate, double &TTC, cv::Mat *visImg)
{
    // compute distance ratios between all matched keypoints
    vector<double> distRatios; // stores the distance ratios for all keypoints between curr. and prev. frame
    for (auto it1 = kptMatches.begin(); it1 != kptMatches.end() - 1; ++it1)
    { // outer kpt. loop
        
        // get current keypoint and its matched partner in the prev. frame
        cv::KeyPoint kpOuterCurr = kptsCurr.at(it1->trainIdx);
        cv::KeyPoint kpOuterPrev = kptsPrev.at(it1->queryIdx);
        
        for (auto it2 = kptMatches.begin() + 1; it2 != kptMatches.end(); ++it2)
        { // inner kpt.-loop
            
            double minDist = 100.0; // min. required distance
            
            // get next keypoint and its matched partner in the prev. frame
            cv::KeyPoint kpInnerCurr = kptsCurr.at(it2->trainIdx);
            cv::KeyPoint kpInnerPrev = kptsPrev.at(it2->queryIdx);
            
            // compute distances and distance ratios
            double distCurr = cv::norm(kpOuterCurr.pt - kpInnerCurr.pt);
            double distPrev = cv::norm(kpOuterPrev.pt - kpInnerPrev.pt);
            
            if (distPrev > std::numeric_limits<double>::epsilon() && distCurr >= minDist)
            { // avoid division by zero
                
                double distRatio = distCurr / distPrev;
                distRatios.push_back(distRatio);
            }
        } // eof inner loop over all matched kpts
    }     // eof outer loop over all matched kpts
    
    // only continue if list of distance ratios is not empty
    if (distRatios.size() == 0)
    {
        TTC = NAN;
        return;
    }
    
    // compute camera-based TTC from distance ratios

    // median instead
    std::sort(distRatios.begin(), distRatios.end());
    double medianDistRatio = distRatios[distRatios.size() / 2];
    
    TTC = (-1.0 / frameRate) / (1 - medianDistRatio);


    
    
}


void computeTTCLidar(std::vector<LidarPoint> &lidarPointsPrev,
                     std::vector<LidarPoint> &lidarPointsCurr, double frameRate, double &TTC)
{
    // auxiliary variables
    double dT = 1 / frameRate;
    double laneWidth = 5.0; // assumed width of the ego lane

    // find the median values
    std::sort(lidarPointsPrev.begin(), lidarPointsPrev.end(), [](LidarPoint pointA, LidarPoint pointB){
        return pointA.x < pointB.x;
    });
    
    double Xpre =  lidarPointsPrev[lidarPointsPrev.size()/2].x;
    
    std::sort(lidarPointsCurr.begin(), lidarPointsCurr.end(), [](LidarPoint pointA, LidarPoint pointB){
        return pointA.x < pointB.x;
    });
    double Xcurr =  lidarPointsCurr[lidarPointsCurr.size()/2].x;

    // compute TTC from both measurements
    TTC = Xcurr * dT / (Xpre - Xcurr);
}


void matchBoundingBoxes(std::vector<cv::DMatch> &matches, std::map<int, int> &bbBestMatches, DataFrame &prevFrame, DataFrame &currFrame)
{
    ///TASK FP.1 -> match list of 3D objects (vector<BoundingBox>) between current and previous frame (implement ->matchBoundingBoxes)_ yin
     
    // yin, find the matching points in the first bounding box and find the bounding box that for all the matches
    for(auto it_boundingbox = currFrame.boundingBoxes.begin(); it_boundingbox != currFrame.boundingBoxes.end(); ++it_boundingbox)
    {
        // calculate the keypoints in the current bounding box
        int id_box_cur = it_boundingbox->boxID;
        int id_box_pre = 0;
        vector<cv::KeyPoint> pre_from_curr_kps;
        for (auto it = matches.begin(); it != matches.end(); ++it)
        {
            
            bool b_x_inbox = currFrame.keypoints[it->trainIdx].pt.x > it_boundingbox->roi.x &&currFrame.keypoints[it->trainIdx].pt.x<it_boundingbox->roi.x+it_boundingbox->roi.width;
            bool b_y_inbox = currFrame.keypoints[it->trainIdx].pt.y > it_boundingbox->roi.y &&currFrame.keypoints[it->trainIdx].pt.y < it_boundingbox->roi.y+it_boundingbox->roi.height;
            if(b_x_inbox && b_y_inbox)
            {
                pre_from_curr_kps.push_back(prevFrame.keypoints[it->queryIdx]);
            }
            //prevFrame.keypoints[it->queryIdx];
            //bbBestMatches =
        }
        
        int nr_in_max  = 1;
        int nr_out_max  = 1;
        float size_box_max = 100000000000000;
        
        // loop all the bouding box of previous fram and see
        for(auto it_bbox_pre = prevFrame.boundingBoxes.begin();it_bbox_pre != prevFrame.boundingBoxes.end(); ++it_bbox_pre)
        {
            float size_box = it_bbox_pre->roi.height*it_bbox_pre->roi.width;
            int nr_in = 1;
            int nr_out = 0;
            
            for(auto it_kps =pre_from_curr_kps.begin();it_kps !=pre_from_curr_kps.end(); ++it_kps)
            {
                bool b_x_inbox = it_kps->pt.x>it_bbox_pre->roi.x &&it_kps->pt.x < it_bbox_pre->roi.x+it_bbox_pre->roi.width;
                bool b_y_inbox = it_kps->pt.y>it_bbox_pre->roi.y &&it_kps->pt.y<it_bbox_pre->roi.y+it_bbox_pre->roi.height;
                if(b_x_inbox && b_y_inbox)
                {
                    ++nr_in;
                }
                else
                {
                    ++nr_out;
                }
            }
            //cout << "debug out "<< "out "<<nr_out <<"in" <<nr_in <<endl;

            if((nr_out/nr_in < nr_out_max/nr_in_max-0.01) && (size_box<=size_box_max))
            {
                if(nr_out/nr_in<0.2)
                {
                    id_box_pre = it_bbox_pre->boxID;
                    size_box_max = size_box;
                    nr_in_max = nr_in;
                    nr_out_max = nr_out;
           //         cout << "debug "<< "out "<<nr_out <<"in" <<nr_in <<endl;
                    bbBestMatches[id_box_pre] = id_box_cur;
           //         cout << "debug "<< "box pre is "<<id_box_pre << "box curr is " << id_box_cur<<endl;
                }
            }
        }


    }
    
            
    
}
