#include "poseEstimate.h"

poseEstimate::poseEstimate(ros::NodeHandle& nh)
{
  //setup ros node handle
  nh_ = nh;
  cloud_in_ = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(new pcl::PointCloud<pcl::PointXYZRGB>);
  cloud_cluster_ = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(new pcl::PointCloud<pcl::PointXYZRGB>);
  cloud_in_filtered_ = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(new pcl::PointCloud<pcl::PointXYZRGB>);
  cloud_in_new_ = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(new pcl::PointCloud<pcl::PointXYZRGB>);
  cloud_in_new_filtered_ = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(new pcl::PointCloud<pcl::PointXYZRGB>);
  cloud_normals_ = pcl::PointCloud<pcl::Normal>::Ptr(new pcl::PointCloud<pcl::Normal>);
  cloud_normals_new_ = pcl::PointCloud<pcl::Normal>::Ptr(new pcl::PointCloud<pcl::Normal>);
  cloud_features_ = FeatureCloudT::Ptr(new FeatureCloudT);
  cloud_features_new_ = FeatureCloudT::Ptr(new FeatureCloudT);
  siftResult_ = pcl::PointCloud<pcl::PointWithScale>::Ptr(new pcl::PointCloud<pcl::PointWithScale>);
  
  object_aligned_ = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(new pcl::PointCloud<pcl::PointXYZRGB>);
  counter = 0;
}


void poseEstimate::setup()
{
  pub_cloud_comb_ = nh_.advertise<pcl::PointCloud<pcl::PointXYZRGB> > ("combined_points", 1);
  pub_cloud_cluster_ = nh_.advertise<pcl::PointCloud<pcl::PointXYZRGB> > ("clustered_points", 1);
  pub_cloud_sift_ = nh_.advertise<pcl::PointCloud<pcl::PointXYZRGB> > ("SIFT_points",1 );
  sub_cloud_in_ = nh_.subscribe<pcl::PointCloud<pcl::PointXYZRGB> >("points2", 1, &poseEstimate::pointCloudCallback, this);
}

void poseEstimate::pointCloudCallback(const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& cloud)
{
  pcl::VoxelGrid<pcl::PointXYZRGB> grid;
  const float leaf = 0.05f;
  grid.setLeafSize (leaf, leaf, leaf);
  pcl::NormalEstimationOMP<pcl::PointXYZRGB, pcl::Normal> normalEstimator;
  pcl::search::KdTree<pcl::PointXYZRGB>::Ptr tree (new pcl::search::KdTree<pcl::PointXYZRGB> ());
  normalEstimator.setSearchMethod(tree); //sets the search method of the normal estimator to KdTree
  //use neighbors in a sphere of radius set in meters
  normalEstimator.setRadiusSearch(0.06); //1cm radius sphere
  //Estimate features
  //pcl::FPFHEstimation<pcl::PointXYZRGB, pcl::Normal, pcl::FPFHSignature33> fest;
  pcl::PFHEstimation<pcl::PointXYZRGB, pcl::Normal, pcl::PFHSignature125> fest;
  pcl::search::KdTree<pcl::PointXYZRGB>::Ptr tree_fest (new pcl::search::KdTree<pcl::PointXYZRGB> ());
  fest.setRadiusSearch (0.1); //radius must be larger than radius used to estimate normals
  fest.setSearchMethod(tree_fest);
  
  if(counter == 0)
  {
    //read in cloud data
    *cloud_in_ = *cloud;
    grid.setInputCloud (cloud_in_);
    grid.filter (*cloud_in_filtered_);
    //estimate the normals of the point cloud
    normalEstimator.setInputCloud(cloud_in_filtered_);
    //computer normal features
    normalEstimator.compute(*cloud_normals_);
    ROS_INFO("Size of normals_: %d", cloud_normals_->points.size());
    fest.setInputCloud (cloud_in_filtered_);
    fest.setInputNormals (cloud_normals_);
    fest.compute(*cloud_features_);
    //counter++;
    //poseEstimate::computeClusterExtraction(cloud_in_);
    poseEstimate::computeSIFTKeypoints();
    poseEstimate::computeTrianglation();
    //std::cin.get();
  }else
  {
    //read in cloud data
    *cloud_in_new_ = *cloud;
    grid.setInputCloud (cloud_in_new_);
    grid.filter (*cloud_in_new_filtered_);  
    //estimate the normals of the point cloud
    normalEstimator.setInputCloud(cloud_in_new_filtered_); 
    //computer normal features
    normalEstimator.compute(*cloud_normals_new_);
    ROS_INFO("Size of normals_new_: %d", cloud_normals_new_->points.size());
    fest.setInputCloud (cloud_in_new_filtered_);
    fest.setInputNormals (cloud_normals_new_);
    fest.compute(*cloud_features_new_);
    
    //Perform alignment
    pcl::SampleConsensusPrerejective<pcl::PointXYZRGB,pcl::PointXYZRGB,pcl::PFHSignature125> align;
    align.setInputSource (cloud_in_filtered_);
    align.setSourceFeatures (cloud_features_);
    align.setInputTarget (cloud_in_new_filtered_);
    align.setTargetFeatures (cloud_features_new_);
    align.setNumberOfSamples (3); // Number of points to sample for generating/prerejecting a pose
    align.setCorrespondenceRandomness (2); // Number of nearest features to use
    align.setSimilarityThreshold (0.6f); // Polygonal edge length similarity threshold
    align.setMaxCorrespondenceDistance (1.5f * leaf); // Set inlier threshold
    align.setInlierFraction (0.25f); // Set required inlier fraction
    align.align (*object_aligned_);
    
    counter = 0;
    
    if (align.hasConverged ())
    {
    // Print results
      Eigen::Matrix4f transformation = align.getFinalTransformation ();
      ROS_INFO ("    | %6.3f %6.3f %6.3f | \n", transformation (0,0), transformation (0,1), transformation (0,2));
      ROS_INFO ("R = | %6.3f %6.3f %6.3f | \n", transformation (1,0), transformation (1,1), transformation (1,2));
      ROS_INFO ("    | %6.3f %6.3f %6.3f | \n", transformation (2,0), transformation (2,1), transformation (2,2));
      ROS_INFO ("\n");
      ROS_INFO ("t = < %0.3f, %0.3f, %0.3f >\n", transformation (0,3), transformation (1,3), transformation (2,3));
      ROS_INFO ("\n");
      ROS_INFO ("Inliers: %i/%i\n", align.getInliers ().size (), cloud_in_->size ());
    
      pcl::PointCloud<pcl::PointXYZRGB>::Ptr Final (new pcl::PointCloud<pcl::PointXYZRGB>);
      pcl::transformPointCloud(*cloud_in_, *Final, align.getFinalTransformation ());
      *cloud_in_new_ += *Final;
      pcl::IterativeClosestPoint<pcl::PointXYZRGB, pcl::PointXYZRGB> icp;
      icp.setInputSource(Final);
      icp.setInputTarget(cloud_in_new_filtered_);
    
      icp.align(*Final);
      pcl::transformPointCloud (*cloud_in_, *Final, icp.getFinalTransformation ());
      //*cloud_in_new_ += *Final;
      pub_cloud_comb_.publish(cloud_in_new_);
    }
  }
  
}

void poseEstimate::computeClusterExtraction(const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& cloud)
{
  pcl::VoxelGrid<pcl::PointXYZRGB> vg; //VoxelGrid Filter to downsample cloud
  pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_filtered (new pcl::PointCloud<pcl::PointXYZRGB>); //filtered cloud container
  pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_f (new pcl::PointCloud<pcl::PointXYZRGB>); //filtered cloud container
  vg.setInputCloud (cloud);
  vg.setLeafSize (0.01f, 0.01f, 0.01f);
  vg.filter (*cloud_filtered); //fill in cloud_filtered with downsampled cloud
  
  // Create the segmentation object for the planar model and set all the parameters
  pcl::SACSegmentation<pcl::PointXYZRGB> seg;//seg object 
  pcl::PointIndices::Ptr inliers (new pcl::PointIndices);//inlier points object shared ptr
  pcl::ModelCoefficients::Ptr coefficients (new pcl::ModelCoefficients); //model coefficient object shared ptr
  pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_plane (new pcl::PointCloud<pcl::PointXYZRGB> ()); //cloud shared ptr object to hold planes
 
  seg.setOptimizeCoefficients (true); //we want to optimize coefficients
  seg.setModelType (pcl::SACMODEL_PLANE);//we want to look for planes
  seg.setMethodType (pcl::SAC_RANSAC); //use RANSAC method
  seg.setMaxIterations (100);
  seg.setDistanceThreshold (0.03); //this is the distance threshold, which determines which points are the inliers 

  int nr_points = (int) cloud_filtered->points.size (); // se
  while (cloud_filtered->points.size () > 0.3 * nr_points) // go through until under 30% of points remain
  {
    // Segment the largest planar component from the remaining cloud
    seg.setInputCloud (cloud_filtered);
    seg.segment (*inliers, *coefficients); //segment and store 
    if (inliers->indices.size () == 0)
    {
      std::cout << "Could not estimate a planar model for the given dataset." << std::endl;
      break;
    }

    // Extract the planar inliers from the input cloud
    pcl::ExtractIndices<pcl::PointXYZRGB> extract;
    extract.setInputCloud (cloud_filtered);
    extract.setIndices (inliers);
    extract.setNegative (false);

    // Get the points associated with the planar surface
    extract.filter (*cloud_plane);
    // Remove the planar inliers, extract the rest
    extract.setNegative (true);
    extract.filter (*cloud_f);
    *cloud_filtered = *cloud_f;
  }
  
  // Creating the KdTree object for the search method of the extraction
  pcl::search::KdTree<pcl::PointXYZRGB>::Ptr tree (new pcl::search::KdTree<pcl::PointXYZRGB>);
  tree->setInputCloud (cloud_filtered);

  std::vector<pcl::PointIndices> cluster_indices;
  pcl::EuclideanClusterExtraction<pcl::PointXYZRGB> ec;
  ec.setClusterTolerance (0.05); // 2cm
  ec.setMinClusterSize (100);
  ec.setMaxClusterSize (30000);
  ec.setSearchMethod (tree);
  ec.setInputCloud (cloud_filtered);
  ec.extract (cluster_indices);

  int j = 0;
  for (std::vector<pcl::PointIndices>::const_iterator it = cluster_indices.begin (); it != cluster_indices.end (); ++it)
  {
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_cluster (new pcl::PointCloud<pcl::PointXYZRGB>);
    cloud_cluster->header.frame_id = cloud->header.frame_id;
    for (std::vector<int>::const_iterator pit = it->indices.begin (); pit != it->indices.end (); pit++)
      cloud_cluster->points.push_back (cloud_filtered->points[*pit]); //*
    cloud_cluster->width = cloud_cluster->points.size ();
    cloud_cluster->height = 1;
    cloud_cluster->is_dense = true;
    pub_cloud_cluster_.publish(cloud_cluster);
    std::cin.get();
    j++;
  }
  
}

void poseEstimate::computeSIFTKeypoints()
{
  // Parameters for sift computation
  const float min_scale = 0.1f;
  const int n_octaves = 6;
  const int n_scales_per_octave = 10;
  const float min_contrast = 0.5f;
  
  
  // Estimate the sift interest points using Intensity values from RGB values
  pcl::SIFTKeypoint<pcl::PointXYZRGB, pcl::PointWithScale> sift;
  pcl::search::KdTree<pcl::PointXYZRGB>::Ptr tree(new pcl::search::KdTree<pcl::PointXYZRGB> ());
  sift.setSearchMethod(tree);
  sift.setScales(min_scale, n_octaves, n_scales_per_octave);
  sift.setMinimumContrast(min_contrast);
  sift.setInputCloud(cloud_in_);
  sift.compute(*siftResult_);
  
  // Copying the pointwithscale to pointxyzrgb so as visualize the cloud
  pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_temp (new pcl::PointCloud<pcl::PointXYZRGB>);
  copyPointCloud(*siftResult_, *cloud_temp);
  cloud_temp->header.frame_id = cloud_in_->header.frame_id;
  pub_cloud_sift_.publish(cloud_temp);
  
}

void poseEstimate::computeTrianglation()
{
  // Concatenate the XYZRGB and normal fields*
  pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr cloud_with_normals (new pcl::PointCloud<pcl::PointXYZRGBNormal>);
  pcl::concatenateFields (*cloud_in_filtered_, *cloud_normals_, *cloud_with_normals);
  //* cloud_with_normals = cloud + normals
  
  // Create search tree*
  pcl::search::KdTree<pcl::PointXYZRGBNormal>::Ptr tree2 (new pcl::search::KdTree<pcl::PointXYZRGBNormal>);
  tree2->setInputCloud (cloud_with_normals);
  
  // Initialize triangulation objects
  pcl::GreedyProjectionTriangulation<pcl::PointXYZRGBNormal> gp3;
  pcl::PolygonMesh triangles;
  
  // Set the maximum distance between connected points (maximum edge length)
  gp3.setSearchRadius (0.025);
  
  // Set typical values for the parameters
  gp3.setMu (2.5);
  gp3.setMaximumNearestNeighbors (100);
  gp3.setMaximumSurfaceAngle(M_PI/4); // 45 degrees
  gp3.setMinimumAngle(M_PI/18); // 10 degrees
  gp3.setMaximumAngle(2*M_PI/3); // 120 degrees
  gp3.setNormalConsistency(false);

  // Get result
  gp3.setInputCloud (cloud_with_normals);
  gp3.setSearchMethod (tree2);
  gp3.reconstruct (triangles);
  
  pcl::io::saveVTKFile ("test.vtk", triangles); 
  
}
