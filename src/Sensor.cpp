#include "Sensor.h"

typedef pcl::PointCloud<pcl::PointXYZRGB> PointCloud;

Sensor::Sensor(ros::NodeHandle& nh) : it_(nh_), device_found(false), proj_helper(NULL)
{
  nh_ = nh;
  color_img.create(720, 1280, CV_8UC3);
  g_aFrames = 0;
  g_cFrames = 0;
  g_dFrames = 0;
}

Sensor::~Sensor()
{
  device_context.stopNodes();

  if(c_node.isSet()) device_context.unregisterNode(c_node);
  if(d_node.isSet()) device_context.unregisterNode(d_node);
  if(a_node.isSet()) device_context.unregisterNode(a_node);
  if(proj_helper) delete proj_helper;
}

void Sensor::setup()
{
   pub_image = it_.advertise("rgb_data",1);
   pub_cloud_image = it_.advertise("points2_image",1);
   pub_cloud = nh_.advertise<PointCloud> ("points2", 1);
   pub_uv = nh_.advertise<PointCloud> ("points_uv", 1);
   pub_camera_info = nh_.advertise<sensor_msgs::CameraInfo>("camera_info",1);
   
   device_context = Context::create("localhost");
   device_context.deviceAddedEvent().connect<Sensor>(this, &Sensor::onDeviceConnected);
   device_context.deviceRemovedEvent().connect<Sensor>(this, &Sensor::onDeviceDisconnected);

   vector<Device> da = device_context.getDevices();
   if(da.size() >= 1)
   {
      device_found = true;
      da[0].nodeAddedEvent().connect<Sensor>(this, &Sensor::onNodeConnected);
      da[0].nodeRemovedEvent().connect<Sensor>(this, &Sensor::onNodeDisconnected);

      vector<Node> na = da[0].getNodes();
      for(int n = 0; n < (int)na.size(); ++n)
      {
         configureNode(na[n]);
      }
  }
  cout << "finish!" << endl;
  
  device_context.startNodes();
  device_context.run();   
}

void Sensor::onNewAudioSample(AudioNode node, AudioNode::NewSampleReceivedData data)
{
}

void Sensor::onNewColorSample(ColorNode node, ColorNode::NewSampleReceivedData data)
{
  color_img.data = (uchar *)(const uint8_t *)data.colorMap;
  
  //Create a sensor_msg::Image for ROS based on the new camera image
  image.header.frame_id = "/map";
  image.header.stamp.nsec = g_cFrames++*1000;
  int count = 0;
  
  int32_t w, h;
  FrameFormat_toResolution(data.captureConfiguration.frameFormat,&w,&h);
  image.width = w;
  image.height = h;
  image.encoding = "bgr8";
  image.data.resize(w*h*3);
  int count2 = w*h*3-1;
   
  for(int i = 0;i < h;i++){
	  for(int j = 0;j < w; j++){	      
      image.data[count2]   = data.colorMap[count2];
      image.data[count2+1] = data.colorMap[count2+1];
      image.data[count2+2] = data.colorMap[count2+2];
      count2-=3;
	  }
  }
  
  // Publish the rgb data
  pub_image.publish(image);
}

void Sensor::onNewDepthSample(DepthNode node, DepthNode::NewSampleReceivedData data)
{
  int count = -1;
  cloud.header.frame_id = "/map";
  cloud.header.stamp = g_dFrames++;
    
  if(!proj_helper)
  {
    proj_helper = new ProjectionHelper(data.stereoCameraParameters);
    stereo_param = data.stereoCameraParameters;
  }
  else if(stereo_param != data.stereoCameraParameters)
  {
    proj_helper->setStereoCameraParameters(data.stereoCameraParameters);
    stereo_param = data.stereoCameraParameters;
  }
  
 
  int32_t w, h;
  FrameFormat_toResolution(data.captureConfiguration.frameFormat,&w,&h);
  
  Vertex p3DPoints[1];
  Point2D p2DPoints[1];

  cloud.height = h;
  cloud.width = w;
  cloud.is_dense = true;
  cloud.points.resize(w*h); 
   
  uint8_t b,g,r;
  uint32_t rgb;
  
  //ROS_INFO("%d %d %d",color_img.at<uint8_t>(400,400));
  
  for(int i = 1;i < h ;i++){
	  for(int j = 1;j < w ; j++){
	     count++;
       cloud.points[count].x = data.verticesFloatingPoint[count].x;
	     cloud.points[count].y = data.verticesFloatingPoint[count].y;
       if(data.verticesFloatingPoint[count].z == -2){
	       cloud.points[count].z = 0;
    	 }else{
	 	     cloud.points[count].z = data.verticesFloatingPoint[count].z;
       }
       //get mapping between depth map and color map
       p3DPoints[0] = data.vertices[count];
       proj_helper->get2DCoordinates ( p3DPoints, p2DPoints, 2, CAMERA_PLANE_COLOR);
       int x_pos = (int)p2DPoints[0].x;
       int y_pos = (int)p2DPoints[0].y;
       if(image.data.size() == 0)
       {
          ROS_INFO("color_img is empty");
       }
       if(y_pos < 0 || y_pos > 720 || x_pos < 0 || x_pos > 1280)
       {
        b = 0;
        g = 0;
        r = 0;
       }else{
        cv::Vec3b bgrPixel = color_img.at<Vec3b>(y_pos,x_pos);
        b = bgrPixel[0];//image.data[(y_pos*w+x_pos)*3+0];
        g = bgrPixel[1];//image.data[(y_pos*w+x_pos)*3+1];
        r = bgrPixel[2];//image.data[(y_pos*w+x_pos)*3+2];
       }
       rgb = ((uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b);
       cloud.points[count].rgb = *reinterpret_cast<float*>(&rgb);
       //cloud.points[count].rgb =  color_img.at<uint8_t>(y_pos,x_pos);
	  }
  }
  //set_up camera info from IntrinsicParameters
  camera_info.header.frame_id = "/map";
  camera_info.header.stamp.nsec = g_dFrames*1000;
  
  camera_info.height = stereo_param.colorIntrinsics.height;
  camera_info.width  = stereo_param.colorIntrinsics.width;
  camera_info.distortion_model = "plumb_bob";
  /*camera_info.D = camera_info.D[5];
  camera_info.D[0] = stereo_param.colorIntrinsics.k1; //k1 first radial distortion coeff
  camera_info.D[1] = stereo_param.colorIntrinsics.k2; //k2 second radial dist. coeff
  camera_info.D[2] = stereo_param.colorIntrinsics.p1; //t1 first tangential distortion coefficient
  camera_info.D[3] = stereo_param.colorIntrinsics.p2; //t2 second tangential distortion coefficient
  camera_info.D[4] = stereo_param.colorIntrinsics.k3; //k3 third radial dist. coeff*/
  camera_info.K[0] = stereo_param.colorIntrinsics.fx; //the focal length along the x axis, expressed in pixel units
  camera_info.K[2] = stereo_param.colorIntrinsics.cx; //the central point along the x axis, expressed in pixel units
  camera_info.K[4] = stereo_param.colorIntrinsics.fy; //the focal length along the y axis, expressed in pixel units
  camera_info.K[5] = stereo_param.colorIntrinsics.cy; //the central point along the y axis, expressed in pixel units  
  camera_info.R[0] = stereo_param.extrinsics.r11; //Seting up the recification matrix
  camera_info.R[1] = stereo_param.extrinsics.r12;
  camera_info.R[2] = stereo_param.extrinsics.r13;
  camera_info.R[3] = stereo_param.extrinsics.r21;
  camera_info.R[4] = stereo_param.extrinsics.r22;
  camera_info.R[5] = stereo_param.extrinsics.r23;
  camera_info.R[6] = stereo_param.extrinsics.r31;
  camera_info.R[7] = stereo_param.extrinsics.r32;
  camera_info.R[8] = stereo_param.extrinsics.r33;
  

  pub_cloud.publish (cloud);
  pub_camera_info.publish (camera_info);
  
  //pcl::toROSMsg(cloud,cloud_image); 
}

void Sensor::configureAudioNode()
{
}

void Sensor::configureColorNode()
{
  c_node.newSampleReceivedEvent().connect<Sensor>(this, &Sensor::onNewColorSample);

  ColorNode::Configuration config = c_node.getConfiguration();
  config.frameFormat = FRAME_FORMAT_WXGA_H;
  config.compression = COMPRESSION_TYPE_MJPEG;
  config.powerLineFrequency = POWER_LINE_FREQUENCY_50HZ;
  config.framerate = 25;

  c_node.setEnableColorMap(true);

  try
  {
    device_context.requestControl(c_node, 0);
    c_node.setConfiguration(config);
  }
  catch(std::exception &ex)
  {
    cerr << "Sensor::configureColorNode()" << endl << ex.what() << endl;
  }
}

void Sensor::configureDepthNode()
{
  d_node.newSampleReceivedEvent().connect<Sensor>(this, &Sensor::onNewDepthSample);

  DepthNode::Configuration config = d_node.getConfiguration();
  config.frameFormat = FRAME_FORMAT_QVGA;
  config.framerate = 25;
  config.mode = DepthNode::CAMERA_MODE_CLOSE_MODE;
  config.saturation = true;

  d_node.setEnableVertices(true);
  d_node.setEnableVerticesFloatingPoint(true);
  d_node.setEnableDepthMapFloatingPoint(true);
  //d_node.setEnableUvMap(true);

  try
  {
    device_context.requestControl(d_node, 0);
    d_node.setConfiguration(config);
  }
  catch(std::exception &ex)
  {
    cerr << "Sensor::configureDepthNode()" << endl << ex.what() << endl;
  }
}

void Sensor::configureNode(Node node)
{
  if((node.is<DepthNode>()) && (!d_node.isSet()))
  {
    d_node = node.as<DepthNode>();
    configureDepthNode();
    device_context.registerNode(node);
  }
  if((node.is<ColorNode>()) && (!c_node.isSet()))
  {
    c_node = node.as<ColorNode>();
    configureColorNode();
    device_context.registerNode(node);
  }
  if((node.is<AudioNode>()) && (!a_node.isSet()))
  {
    a_node = node.as<AudioNode>();
    configureAudioNode();
    device_context.registerNode(node);
  }
}

void Sensor::onNodeConnected(Device device, Device::NodeAddedData data)
{
  configureNode(data.node);
}

void Sensor::onNodeDisconnected(Device device, Device::NodeRemovedData data)
{
  if (data.node.is<AudioNode>() && (data.node.as<AudioNode>() == a_node))
    a_node.unset();
  if (data.node.is<ColorNode>() && (data.node.as<ColorNode>() == c_node))
    c_node.unset();
  if (data.node.is<DepthNode>() && (data.node.as<DepthNode>() == d_node))
    d_node.unset();
  printf("Node disconnected\n");
}

void Sensor::onDeviceConnected(Context context, Context::DeviceAddedData data)
{
  if(!device_found)
  {
    data.device.nodeAddedEvent().connect<Sensor>(this, &Sensor::onNodeConnected);
    data.device.nodeRemovedEvent().connect<Sensor>(this, &Sensor::onNodeDisconnected);
    device_found = true;
  }
}

void Sensor::onDeviceDisconnected(Context context, Context::DeviceRemovedData data)
{
  device_found = false;
}

void Sensor::spin()
{
  ros::Rate rate (30);
  while (ros::ok ())
  {
    ros::spinOnce ();
    rate.sleep ();
   }
}
