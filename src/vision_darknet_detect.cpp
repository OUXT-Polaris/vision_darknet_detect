/*
 * Copyright 2018-2019 Autoware Foundation. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ********************
 *  v1.0: amc-nu (abrahammonrroy@yahoo.com)
 *
 * yolo3_node.cpp
 *
 *  Created on: April 4th, 2018
 */
#include "vision_darknet_detect.h"

#if (CV_MAJOR_VERSION <= 2)
#include <opencv2/contrib/contrib.hpp>
#else
#include "gencolors.cpp"
#endif

#include <unique_id/unique_id.h>

namespace darknet
{
    uint32_t Yolo3Detector::get_network_height()
    {
        return darknet_network_->h;
    }
    uint32_t Yolo3Detector::get_network_width()
    {
        return darknet_network_->w;
    }
    void Yolo3Detector::load(std::string& in_model_file, std::string& in_trained_file, double in_min_confidence, double in_nms_threshold)
    {
        min_confidence_ = in_min_confidence;
        nms_threshold_ = in_nms_threshold;
        darknet_network_ = parse_network_cfg(&in_model_file[0]);
        load_weights(darknet_network_, &in_trained_file[0]);
        set_batch_network(darknet_network_, 1);

        layer output_layer = darknet_network_->layers[darknet_network_->n - 1];
        darknet_boxes_.resize(output_layer.w * output_layer.h * output_layer.n);
    }

    Yolo3Detector::~Yolo3Detector()
    {
        free_network(darknet_network_);
    }

    std::vector< RectClassScore<float> > Yolo3Detector::detect(image& in_darknet_image)
    {
        return forward(in_darknet_image);
    }

    image Yolo3Detector::convert_image(const sensor_msgs::ImageConstPtr& msg)
    {
        if (msg->encoding != sensor_msgs::image_encodings::BGR8)
        {
            ROS_ERROR("Unsupported encoding");
            exit(-1);
        }

        auto data = msg->data;
        uint32_t height = msg->height, width = msg->width, offset = msg->step - 3 * width;
        uint32_t i = 0, j = 0;
        image im = make_image(width, height, 3);

        for (uint32_t line = height; line; line--)
        {
            for (uint32_t column = width; column; column--)
            {
                for (uint32_t channel = 0; channel < 3; channel++)
                    im.data[i + width * height * channel] = data[j++] / 255.;
                i++;
            }
            j += offset;
        }

        if (darknet_network_->w == (int) width && darknet_network_->h == (int) height)
        {
            return im;
        }
        image resized = resize_image(im, darknet_network_->w, darknet_network_->h);
        free_image(im);
        return resized;
    }

    std::vector< RectClassScore<float> > Yolo3Detector::forward(image& in_darknet_image)
    {
        float * in_data = in_darknet_image.data;
        float *prediction = network_predict(darknet_network_, in_data);
        layer output_layer = darknet_network_->layers[darknet_network_->n - 1];

        output_layer.output = prediction;
        int nboxes = 0;
        int num_classes = output_layer.classes;
        detection *darknet_detections = get_network_boxes(darknet_network_, darknet_network_->w, darknet_network_->h, min_confidence_, .5, NULL, 0, &nboxes);

        do_nms_sort(darknet_detections, nboxes, num_classes, nms_threshold_);

        std::vector< RectClassScore<float> > detections;

        for (int i = 0; i < nboxes; i++)
        {
            int class_id = -1;
            float score = 0.f;
            //find the class
            for(int j = 0; j < num_classes; ++j){
                if (darknet_detections[i].prob[j] >= min_confidence_){
                    if (class_id < 0) {
                        class_id = j;
                        score = darknet_detections[i].prob[j];
                    }
                }
            }
            //if class found
            if (class_id >= 0)
            {
                RectClassScore<float> detection;

                detection.x = darknet_detections[i].bbox.x - darknet_detections[i].bbox.w/2;
                detection.y = darknet_detections[i].bbox.y - darknet_detections[i].bbox.h/2;
                detection.w = darknet_detections[i].bbox.w;
                detection.h = darknet_detections[i].bbox.h;
                detection.score = score;
                detection.class_type = class_id;
                //std::cout << detection.toString() << std::endl;

                detections.push_back(detection);
            }
        }
        //std::cout << std::endl;
        return detections;
    }
}  // namespace darknet

///////////////////

void Yolo3DetectorNode::convert_rect_to_image_obj(std::vector< RectClassScore<float> >& in_objects,std_msgs::Header header,vision_msgs::Detection2DArray& detections)
{
    detections = vision_msgs::Detection2DArray();
    detections.header = header;
    for (unsigned int i = 0; i < in_objects.size(); ++i)
    {
        vision_msgs::Detection2D detection;
        vision_msgs::BoundingBox2D bbox;
        bbox.center.x = (in_objects[i].x/image_ratio_) - image_left_right_border_/image_ratio_ + in_objects[i].w/image_ratio_*0.5;
        bbox.center.y = (in_objects[i].y/image_ratio_) - image_top_bottom_border_/image_ratio_ + in_objects[i].h/image_ratio_*0.5;
        bbox.center.theta = 0;
        bbox.size_x = in_objects[i].w/image_ratio_;
        bbox.size_y = in_objects[i].h/image_ratio_;
        vision_msgs::ObjectHypothesisWithPose hypo;
        hypo.score = in_objects[i].score;
        hypo.id = in_objects[i].class_type;
        detection.bbox = bbox;
        detection.results.push_back(hypo);
        detection.is_tracking = false;
        detection.header = header;
        detection.detection_id = unique_id::toMsg(unique_id::fromRandom());
        detections.detections.push_back(detection);
    }
}

void Yolo3DetectorNode::convert_rect_to_image_obj(std::vector< RectClassScore<float> >& in_objects,
    jsk_recognition_msgs::RectArray& out_rect,jsk_recognition_msgs::ClassificationResult& out_class)
{
    out_rect = jsk_recognition_msgs::RectArray();
    out_class = jsk_recognition_msgs::ClassificationResult();
    for (unsigned int i = 0; i < in_objects.size(); ++i)
    {
        jsk_recognition_msgs::Rect rect;
        rect.x = (in_objects[i].x /image_ratio_) - image_left_right_border_/image_ratio_;
        rect.y = (in_objects[i].y /image_ratio_) - image_top_bottom_border_/image_ratio_;
        rect.width = in_objects[i].w /image_ratio_;
        rect.height = in_objects[i].h /image_ratio_;
        if (in_objects[i].x < 0)
        {
            rect.x = 0;
        }
        if (in_objects[i].y < 0)
        {
            rect.y = 0;
        }
        if (in_objects[i].w < 0)
        {
            rect.width = 0;
        }
        if (in_objects[i].h < 0)
        {
            rect.height = 0;
        }
        out_rect.rects.push_back(rect);
        if (use_coco_names_)
        {
            out_class.label_names.push_back(in_objects[i].GetClassString());
            out_class.labels.push_back(in_objects[i].class_type);
        }
        else
        {
            if (in_objects[i].class_type < custom_names_.size())
            {
                out_class.label_names.push_back(custom_names_[in_objects[i].class_type]);
                out_class.labels.push_back(in_objects[i].class_type);
            }
            else
            {
                out_class.label_names.push_back("unknown");
                out_class.labels.push_back(255);
            }
        }
        out_class.label_proba.push_back(in_objects[i].score);
        out_class.classifier = "vision_darknet_detect";
        if(!use_coco_names_)
        {
            out_class.target_names = custom_names_;
        }
    }
}

void Yolo3DetectorNode::rgbgr_image(image& im)
{
    int i;
    for(i = 0; i < im.w*im.h; ++i)
    {
        float swap = im.data[i];
        im.data[i] = im.data[i+im.w*im.h*2];
        im.data[i+im.w*im.h*2] = swap;
    }
}

image Yolo3DetectorNode::convert_ipl_to_image(const sensor_msgs::ImageConstPtr& msg)
{
    cv_bridge::CvImagePtr cv_image = cv_bridge::toCvCopy(msg, "bgr8");//toCvCopy(image_source, sensor_msgs::image_encodings::BGR8);
    cv::Mat mat_image = cv_image->image;

    int network_input_width = yolo_detector_.get_network_width();
    int network_input_height = yolo_detector_.get_network_height();

    int image_height = msg->height,
            image_width = msg->width;

    IplImage ipl_image;
    cv::Mat final_mat;

    if (network_input_width!=image_width
        || network_input_height != image_height)
    {
        //final_mat = cv::Mat(network_input_width, network_input_height, CV_8UC3, cv::Scalar(0,0,0));
        image_ratio_ = (double ) network_input_width /  (double)mat_image.cols;

        cv::resize(mat_image, final_mat, cv::Size(), image_ratio_, image_ratio_);
        image_top_bottom_border_ = abs(final_mat.rows-network_input_height)/2;
        image_left_right_border_ = abs(final_mat.cols-network_input_width)/2;
        cv::copyMakeBorder(final_mat, final_mat,
                           image_top_bottom_border_, image_top_bottom_border_,
                           image_left_right_border_, image_left_right_border_,
                           cv::BORDER_CONSTANT, cv::Scalar(0,0,0));
    }
    else
        final_mat = mat_image;

    ipl_image = final_mat;

    unsigned char *data = (unsigned char *)ipl_image.imageData;
    int h = ipl_image.height;
    int w = ipl_image.width;
    int c = ipl_image.nChannels;
    int step = ipl_image.widthStep;
    int i, j, k;

    image darknet_image = make_image(w, h, c);

    for(i = 0; i < h; ++i){
        for(k= 0; k < c; ++k){
            for(j = 0; j < w; ++j){
                darknet_image.data[k*w*h + i*w + j] = data[i*step + j*c + k]/255.;
            }
        }
    }
    rgbgr_image(darknet_image);
    return darknet_image;
}

void Yolo3DetectorNode::image_callback(const sensor_msgs::ImageConstPtr& in_image_message)
{
    cv_bridge::CvImagePtr cv_ptr;
    try
    {
        cv_ptr = cv_bridge::toCvCopy(in_image_message, sensor_msgs::image_encodings::BGR8);
    }
    catch (cv_bridge::Exception& e)
    {
        ROS_ERROR("cv_bridge exception: %s", e.what());
        return;
    }
    std::vector< RectClassScore<float> > detections;

    darknet_image_ = convert_ipl_to_image(in_image_message);

    detections = yolo_detector_.detect(darknet_image_);

    //Prepare Output message
    jsk_recognition_msgs::RectArray rects;
    jsk_recognition_msgs::ClassificationResult class_result;
    rects.header = in_image_message->header;
    class_result.header = in_image_message->header;

    if(output_as_vision_msgs_)
    {
        vision_msgs::Detection2DArray detections_msg;
        convert_rect_to_image_obj(detections, in_image_message->header, detections_msg);
        detection_pub_.publish(detections_msg);
    }
    else
    {
        convert_rect_to_image_obj(detections, rects, class_result);
        rect_pub_.publish(rects);
        class_pub_.publish(class_result);
    }

    if(publish_labeled_image_)
    {
        int rects_size = rects.rects.size();
        for(int i=0; i<rects_size; i++)
        {
            if(class_result.labels[i] == 255)
            {
                continue;
            }
            int x0 = rects.rects[i].x;
            int y0 = rects.rects[i].y;
            int x1 = rects.rects[i].x + rects.rects[i].width;
            int y1 = rects.rects[i].y + rects.rects[i].height;
            cv::rectangle(cv_ptr->image, {x0, y0}, {x1, y1}, colors_[class_result.labels[i]], 2);
            int text_y0 = y0 + ((y0 > 30) ? -15 : 15);
            std::string label = class_result.label_names[i] + ":" + std::to_string(class_result.label_proba[i]*100.0) + "%";
            cv::putText(cv_ptr->image, label, {x0, text_y0}, cv::FONT_HERSHEY_SIMPLEX, 0.5, colors_[class_result.labels[i]], 1, false);
        }
    }
    image_pub_.publish(cv_ptr->toImageMsg());

    free(darknet_image_.data);
}

std::vector<std::string> Yolo3DetectorNode::read_custom_names_file(const std::string& in_names_path)
{
    std::ifstream file(in_names_path);
    std::string str;
    std::vector<std::string> names;
    while (std::getline(file, str))
    {
        names.push_back(str);
        std::cout << str <<  std::endl;
    }
    return names;
}

void Yolo3DetectorNode::Run()
{
    //ROS STUFF
    ros::NodeHandle private_node_handle("~");//to receive args

    private_node_handle.getParam("output_as_vision_msgs", output_as_vision_msgs_);

    //RECEIVE IMAGE TOPIC NAME
    std::string image_raw_topic_str;
    if (private_node_handle.getParam("image_raw_node", image_raw_topic_str))
    {
        ROS_INFO("Setting image node to %s", image_raw_topic_str.c_str());
    }
    else
    {
        ROS_INFO("No image node received, defaulting to /image_raw, you can use _image_raw_node:=YOUR_TOPIC");
        image_raw_topic_str = "/image_raw";
    }

    std::string network_definition_file;
    std::string pretrained_model_file, names_file;
    if (private_node_handle.getParam("network_definition_file", network_definition_file))
    {
        ROS_INFO("Network Definition File (Config): %s", network_definition_file.c_str());
    }
    else
    {
        ROS_INFO("No Network Definition File was received. Finishing execution.");
        return;
    }
    if (private_node_handle.getParam("pretrained_model_file", pretrained_model_file))
    {
        ROS_INFO("Pretrained Model File (Weights): %s", pretrained_model_file.c_str());
    }
    else
    {
        ROS_INFO("No Pretrained Model File was received. Finishing execution.");
        return;
    }

    if (private_node_handle.getParam("names_file", names_file))
    {
        ROS_INFO("Names File: %s", names_file.c_str());
        use_coco_names_ = false;
        custom_names_ = read_custom_names_file(names_file);
    }
    else
    {
        ROS_INFO("No Names file was received. Using default COCO names.");
        use_coco_names_ = true;
    }

    private_node_handle.param<float>("score_threshold", score_threshold_, 0.5);
    ROS_INFO("[%s] score_threshold: %f",__APP_NAME__, score_threshold_);

    private_node_handle.param<float>("nms_threshold", nms_threshold_, 0.45);
    ROS_INFO("[%s] nms_threshold: %f",__APP_NAME__, nms_threshold_);

    private_node_handle.param<bool>("publish_labeled_image", publish_labeled_image_, false);

    ROS_INFO("Initializing Yolo on Darknet...");
    yolo_detector_.load(network_definition_file, pretrained_model_file, score_threshold_, nms_threshold_);
    ROS_INFO("Initialization complete.");

    #if (CV_MAJOR_VERSION <= 2)
        cv::generateColors(colors_, 80);
    #else
        generateColors(colors_, 80);
    #endif

    rect_pub_ = private_node_handle.advertise<jsk_recognition_msgs::RectArray>("rect",1);

    if(output_as_vision_msgs_)
    {
        detection_pub_ = private_node_handle.advertise<vision_msgs::Detection2DArray>("detection",1);
    }
    else
    {
        class_pub_ = private_node_handle.advertise<jsk_recognition_msgs::ClassificationResult>("class",1);
    }
    image_pub_ = private_node_handle.advertise<sensor_msgs::Image>("labeled_image",1);

    ROS_INFO("Subscribing to... %s", image_raw_topic_str.c_str());
    subscriber_image_raw_ = node_handle_.subscribe(image_raw_topic_str, 1, &Yolo3DetectorNode::image_callback, this);

    std::string config_topic("/config");
    config_topic += "/Yolo3";

    ROS_INFO_STREAM( __APP_NAME__ << "" );

    ros::spin();
    ROS_INFO("END Yolo");

}
