#include "lab56pkg/lab56.h"

extern ImageConverter* ic_ptr; //global pointer from the lab56.cpp

#define SPIN_RATE 20  /* Hz */

bool isReady=1;
bool pending=0;

float SuctionValue = 0.0;

bool leftclickdone = 1;
bool rightclickdone = 1;

/*****************************************************
* Functions in class:
* **************************************************/	

void suction(ros::ServiceClient srv_SetIO, ur_msgs::SetIO srv, float state) {
  srv.request.fun = 1;
  srv.request.pin = 0; // Digital Output 0
  srv.request.state = state; //Set DO0 off
  if (srv_SetIO.call(srv)) {
    ROS_INFO("True: Switched Suction OFF");
  } else {
    ROS_INFO("False");
  }
}

//constructor(don't modify) 
ImageConverter::ImageConverter():it_(nh_)
{
    // Subscrive to input video feed and publish output video feed
    image_sub_ = it_.subscribe("/cv_camera_node/image_raw", 1, 
    	&ImageConverter::imageCb, this);
    image_pub_ = it_.advertise("/image_converter/output_video", 1);
    namedWindow(OPENCV_WINDOW);   
    pub_command=nh_.advertise<ece470_ur3_driver::command>("ur3/command",10);
    sub_position=nh_.subscribe("ur3/position",1,&ImageConverter::position_callback,this); 

	sub_io_states=nh_.subscribe("ur_driver/io_states",1,&ImageConverter::suction_callback,this);
	
	srv_SetIO = nh_.serviceClient<ur_msgs::SetIO>("ur_driver/set_io");


    driver_msg.destination=lab_invk(.043,-.437,0.152,-90);

	//publish the point to the robot
    ros::Rate loop_rate(SPIN_RATE); // Initialize the rate to publish to ur3/command
	int spincount = 0;
	driver_msg.duration = 3.0;
	pub_command.publish(driver_msg);  // publish command, but note that is possible that
										  // the subscriber will not receive this message.
	spincount = 0;
	while (isReady) { // Waiting for isReady to be false meaning that the driver has the new command
		ros::spinOnce();  // Allow other ROS functionallity to run
		loop_rate.sleep(); // Sleep and wake up at 1/20 second (1/SPIN_RATE) interval
		if (spincount > SPIN_RATE) {  // if isReady does not get set within 1 second re-publish
			pub_command.publish(driver_msg);
			ROS_INFO_STREAM("Just Published again driver_msg");
			spincount = 0;
		}
		spincount++;  // keep track of loop count
	}
	ROS_INFO_STREAM("waiting for rdy");  // Now wait for robot arm to reach the commanded waypoint.
	
	while(!isReady)
	{
		ros::spinOnce();
		loop_rate.sleep();
	}
	ROS_INFO_STREAM("Ready for new point");

}

//destructor(don't modify)
ImageConverter::~ImageConverter()
{
    cv::destroyWindow(OPENCV_WINDOW);
}

void ImageConverter::position_callback(const ece470_ur3_driver::positions::ConstPtr& msg)
{
	isReady=msg->isReady;
	pending=msg->pending;
}

void ImageConverter::suction_callback(const ur_msgs::IOStates::ConstPtr& msg)
{
	SuctionValue = msg->analog_in_states[0].state;
}


//subscriber callback function, will be called when there is a new image read by camera
void ImageConverter::imageCb(const sensor_msgs::ImageConstPtr& msg)
{  
    cv_bridge::CvImagePtr cv_ptr;
    try
    {
      cv_ptr = cv_bridge::toCvCopy(msg, sensor_msgs::image_encodings::BGR8);
    }
    catch (cv_bridge::Exception& e)
    {
      ROS_ERROR("cv_bridge exception: %s", e.what());
      return;
    } 
    // create an gray scale version of image
    Mat gray_image;
	cvtColor( cv_ptr->image, gray_image, CV_BGR2GRAY );  
    // convert to black and white img, then associate objects:  

	Mat bw_image;
	adaptiveThreshold(gray_image,bw_image,255,0,0,151,5);
	//adaptiveThreshold(scr,dst,MAXVALUE,adaptiveMethod,thresholdType,blocksize,C);
	//adaptiveMethod = 0, ADAPTIVE_THRESH_MEAN_C
	//thresholdType = 0, BINARY
	//blocksize
	//C constant subtracted from tz.  
	
	
// FUNCTION you will be completing
    Mat associate_image = associateObjects(bw_image); // find associated objects

    // Update GUI Window
    imshow("Image window", cv_ptr->image);
    imshow("gray_scale", gray_image);
    imshow("black and white", bw_image);
    imshow("associate objects", associate_image);
    waitKey(3);
    // Output some video stream
    image_pub_.publish(cv_ptr->toImageMsg());
} 

/*****************************************************
	 * Function for Lab 5
* **************************************************/	
// Take a grayscale image as input and return an thresholded image.
// You will implement your algorithm for calculating threshold here.
Mat ImageConverter::thresholdImage(Mat gray_img)
{
		int   totalpixels;
		Mat bw_img  = gray_img.clone(); // copy input image to a new image
		totalpixels	  = gray_img.rows*gray_img.cols;			// total number of pixels in image
		uchar graylevel; // use this variable to read the value of a pixel
		int zt=0; // threshold grayscale value 
		
		

		zt = 100;  // you will be finding this automatically 


		//std::cout<<zt<<std::endl;
		// threshold the image
		for(int i=0; i<totalpixels; i++)
		{
			graylevel = gray_img.data[i];	
			if(graylevel>zt) bw_img.data[i]= 255; // set rgb to 255 (white)
			else             bw_img.data[i]= 0; // set rgb to 0   (black)
		}	
	return bw_img;	
}
/*****************************************************
	 * Function for Lab 5
* **************************************************/
// Take an black and white image and find the object it it, returns an associated image with different color for each image
// You will implement your algorithm for rastering here
Mat ImageConverter::associateObjects(Mat bw_img)
{
	//initiallize the variables you will use
	int height,width; // number of rows and colums of image
	int red, green, blue; //used to assign color of each objects
	//uchar pixel; //used to read pixel value of input image 
	height = bw_img.rows;
	width = bw_img.cols;
	int num = 0;
	int num_of_labels = height * width;
	// initialize an array of labels, assigning a label number to each pixel in the image
	// this create a 2 dimensional array pixellabel[row][col]
	int ** pixellabel = new int*[height];
	for (int i=0;i<height;i++) {
		pixellabel[i] = new int[width];
	}

	int label[num_of_labels];
	int *equiv[num_of_labels];

	// noise cancelation
	int object_size[num_of_labels];
	int object_cutoff=500;
	int object_cutoff_max=1500;

	int objects_final[num_of_labels];

	for (int i = 0; i < num_of_labels; i++) {
		object_size[i] = 0;
		objects_final[i] = 0;
	}

	for (int i = 0; i < num_of_labels; i++) {
		equiv[i] = &label[i];
	}

	int foreground = 0;
	int background = -1;

	for (int i = 0; i < height; i++) {
		for (int j = 0; j < width; j++) {
			if (bw_img.data[i*width + j] == 0) {
				pixellabel[i][j] = foreground;
			}
			else {
				pixellabel[i][j] = background;
			}  
		}
	}

	int labelnum = 0;
	int pixel;
	int left;
	int above;
	int smaller_base_label, max, min;

	for (int i = 0; i < height; i++) {
		for (int j = 0; j < width; j++) {
			pixel = pixellabel[i][j];
			if (j-1 >= 0) {
				left = pixellabel[i][j-1];
			} else {
				left = background;
			}
			if (i-1 >= 0) {
				above = pixellabel[i-1][j];
			} else {
				above = background;
			}

			if (pixel >= foreground) {
				if (left == background && above == background) {
					pixellabel[i][j] = labelnum;
					label[labelnum] = labelnum;
					//might have bug here
					if (labelnum < num_of_labels - 1) {
						labelnum++;
					} else {
						labelnum = num_of_labels - 1;
					}
				}

				if (left >= foreground && above == background) {
					pixellabel[i][j] = left;
				}

				if (left == background && above >= foreground) {
					pixellabel[i][j] = above;
				}

				//Equivalence case
				if (left >= foreground && above >= foreground) {
					if (*equiv[left] <= *equiv[above]) {
						smaller_base_label = *equiv[left];
						min = left;
						max = above;
					} else {
						smaller_base_label = *equiv[above];
						min = above;
						max = left;
					}

					pixellabel[i][j] = smaller_base_label;
					*equiv[max] = *equiv[min];
					equiv[max] = equiv[min];

				}
			}
		}
	}

	//second raster
	for (int i = 0; i < height; i++) {
		for (int j = 0; j < width; j++) {
			pixel = pixellabel[i][j];
			if (pixel >= foreground) {
				pixellabel[i][j] = *equiv[pixel];
				// noise reduction
				object_size[pixellabel[i][j]]++;
			}
		}
	}

	int small_objects[num_of_labels];
	const int object_limit = 9;
	//row = 0, col = 1
	static int centroids[object_limit][2];
	for (int i = 0; i < object_limit; i++) {

		centroids[i][0] = 0;
		centroids[i][1] = 0;
	}
	int count_objects = 0;
	for (int i=0; i<=num_of_labels; i++){
		if (object_size[i]<=object_cutoff || object_size[i]>=object_cutoff_max){
			small_objects[i]=1;
		}
		else{
			if (count_objects < object_limit) {
				small_objects[i]=0;
				objects_final[count_objects] = i;
			
				for (int row = 0; row < height; row++) {
					for (int col = 0; col < width; col++) {
						if (pixellabel[row][col] == i) {
							centroids[count_objects][0] += row;
							centroids[count_objects][1] += col;
						}
					}
				}
				centroids[count_objects][0] /= object_size[i];
				centroids[count_objects][1] /= object_size[i];
				
				count_objects++;
			}
		}
	}

	// for (int i = 0; i < count_objects; i++) {
	// 	std::cout << "Object(" << i << "): y (row) = " << centroids[i][0] << ", x (col) = " << centroids[i][1] << std::endl;
	// }

	// assign UNIQUE color to each object
	Mat associate_img = Mat::zeros( bw_img.size(), CV_8UC3 ); // function will return this image
	Vec3b color;
	for(int row=0; row<height; row++)
	{
		for(int col=0; col<width; col++)
		{
			if (pixellabel[row][col] == background || small_objects[pixellabel[row][col]] == 1) {
				red = 255;
				green = 255;
				blue = 255;
			} else {

				if (pixellabel[row][col] == objects_final[0]) {
					red    = 255; // you can change color of each objects here
					green  = 0;
					blue   = 0;	
				} else if (pixellabel[row][col] == objects_final[1]) {
					red    = 0;
					green  = 255;
					blue   = 0;
				} else if (pixellabel[row][col] == objects_final[2]) {
					red    = 0;
					green  = 0;
					blue   = 255;
				} else if (pixellabel[row][col] == objects_final[3]) {
					red    = 255; // you can change color of each objects here
					green  = 255;
					blue   = 0;	
				} else if (pixellabel[row][col] == objects_final[4]) {
					red    = 0;
					green  = 255;
					blue   = 255;
				} else if (pixellabel[row][col] == objects_final[5]) {
					red    = 255;
					green  = 0;
					blue   = 255;
				} else if (pixellabel[row][col] == objects_final[6]) {
					red    = 128;
					green  = 255;
					blue   = 0;
				} else if (pixellabel[row][col] == objects_final[7]) {
					red    = 0;
					green  = 255;
					blue   = 128;
				} else if (pixellabel[row][col] == objects_final[8]) {
					red    = 128;
					green  = 128;
					blue   = 128;
				} else {
					red    = 0;
					green = 0;
					blue   = 0;
				}
			}

			color[0] = blue;
			color[1] = green;
			color[2] = red;
			associate_img.at<Vec3b>(Point(col,row)) = color;

		}
	}
	
	int crosshair_dim = 5;
	int cl, cr, rt, rb;
	color[0] = 0;
	color[1] = 0;
	color[2] = 0;
	for (int i = 0; i < count_objects; i++) {
		for (int j = 0; j <= crosshair_dim; j++) {
			cl = centroids[i][1] - j;
			cr = centroids[i][1] + j;
			rt = centroids[i][0] - j;
			rb = centroids[i][0] + j;
			associate_img.at<Vec3b>(Point(cl,centroids[i][0])) = color;
			associate_img.at<Vec3b>(Point(cr,centroids[i][0])) = color;
			associate_img.at<Vec3b>(Point(centroids[i][1],rt)) = color;
			associate_img.at<Vec3b>(Point(centroids[i][1],rb)) = color;
		}
	}
	return associate_img;
}

void camera_cal(int row, int col, double * ret) {
	const double height = 480; const int width = 640;
	const double orig_r = height / 2;
 	const double orig_c = width / 2;

 	const double beta = 744;
// may need to change a bit 	
    const double theta= .0233;
//guess n chech    
    const double tx=0.231247;
    const double ty=-0.06213;
    double xc, yc, xw, yw;


	xc=-1*(col-orig_c)/beta;
	yc=-1*(row-orig_r)/beta;

	cout << "xc: " << xc << ", yc: " << yc << endl;

	// xw=cos(theta)*xc+sin(theta)*yc-tx*cos(theta)-ty*sin(theta);
	// yw=-sin(theta)*xc+cos(theta)*yc+tx*sin(theta)-ty*cos(theta);

	// cout << xw << ", " << yw << endl;

	xw = xc + tx;
	yw = yc + ty;
	cout << "xw: " << xw << ", yw: " << yw << endl;

	ret[0] = xw;
	ret[1] = yw;
}





/*****************************************************
	*Function for Lab 6
 * **************************************************/
 //This is a call back function of mouse click, it will be called when there's a click on the video window.
 //You will write your coordinate transformation in onClick function.
 //By calling onClick, you can use the variables calculated in the class function directly and use publisher
 //initialized in constructor to control the robot.
 //lab4 and lab3 functions can be used since it is included in the "lab4.h" 
void onMouse(int event, int x, int y, int flags, void* userdata)
{
		//cout << "here" << endl;
		ic_ptr->onClick(event,x,y,flags,userdata);

}
void ImageConverter::onClick(int event,int x, int y, int flags, void* userdata)
{
	double world[2];
	ur_msgs::SetIO srv;
	// For use with Lab 6
	// If the robot is holding a block, place it at the designated row and column. 
	if  ( event == EVENT_LBUTTONDOWN ) //if left click, do nothing other than printing the clicked point
	{  
		if (leftclickdone == 1) {
			leftclickdone = 0;  // code started
			ROS_INFO_STREAM("left click:  (" << x << ", " << y << ")");  //the point you clicked

			// put your left click code here
			camera_cal(x, y, world);

			driver_msg.destination=lab_invk(world[0],world[1],0.022,-90);

			//publish the point to the robot
		    ros::Rate loop_rate(SPIN_RATE); // Initialize the rate to publish to ur3/command
			int spincount = 0;
			driver_msg.duration = 3.0;
			pub_command.publish(driver_msg);  // publish command, but note that is possible that
												  // the subscriber will not receive this message.
			spincount = 0;
			while (isReady) { // Waiting for isReady to be false meaning that the driver has the new command
				ros::spinOnce();  // Allow other ROS functionallity to run
				loop_rate.sleep(); // Sleep and wake up at 1/20 second (1/SPIN_RATE) interval
				if (spincount > SPIN_RATE) {  // if isReady does not get set within 1 second re-publish
					pub_command.publish(driver_msg);
					ROS_INFO_STREAM("Just Published again driver_msg");
					spincount = 0;
				}
				spincount++;  // keep track of loop count
			}
			ROS_INFO_STREAM("waiting for rdy");  // Now wait for robot arm to reach the commanded waypoint.
			
			while(!isReady)
			{
				ros::spinOnce();
				loop_rate.sleep();
			}
			ROS_INFO_STREAM("Ready for new point");

			suction(this->srv_SetIO, srv, 1.0);
			int err_count = 0;
			while(!SuctionValue)
			{
				ros::spinOnce();
				loop_rate.sleep();
				err_count++;
				if (err_count > 50){
				  suction(srv_SetIO, srv, 0.0);
				  ROS_ERROR("NO BLOCK FOUND");
				  ros::spin();
				}
			}		

			driver_msg.destination=lab_invk(world[0],world[1],0.07,-90);

			//publish the point to the robot
			driver_msg.duration = 3.0;
			pub_command.publish(driver_msg);  // publish command, but note that is possible that
												  // the subscriber will not receive this message.
			spincount = 0;
			while (isReady) { // Waiting for isReady to be false meaning that the driver has the new command
				ros::spinOnce();  // Allow other ROS functionallity to run
				loop_rate.sleep(); // Sleep and wake up at 1/20 second (1/SPIN_RATE) interval
				if (spincount > SPIN_RATE) {  // if isReady does not get set within 1 second re-publish
					pub_command.publish(driver_msg);
					ROS_INFO_STREAM("Just Published again driver_msg");
					spincount = 0;
				}
				spincount++;  // keep track of loop count
			}
			ROS_INFO_STREAM("waiting for rdy");  // Now wait for robot arm to reach the commanded waypoint.
			
			while(!isReady)
			{
				ros::spinOnce();
				loop_rate.sleep();
			}
			ROS_INFO_STREAM("Ready for new point");

			leftclickdone = 1; // code finished
		} else {
			ROS_INFO_STREAM("Previous Left Click not finshed, IGNORING this Click"); 
		}
	}
	else if  ( event == EVENT_RBUTTONDOWN )//if right click, find nearest centroid,
	{
		if (rightclickdone == 1) {  // if previous right click not finished ignore
			rightclickdone = 0;  // starting code
			ROS_INFO_STREAM("right click:  (" << x << ", " << y << ")");  //the point you clicked

			// put your right click code here
			camera_cal(x, y, world);

			driver_msg.destination=lab_invk(world[0],world[1],0.022,-90);

			//publish the point to the robot
		    ros::Rate loop_rate(SPIN_RATE); // Initialize the rate to publish to ur3/command
			int spincount = 0;
			driver_msg.duration = 3.0;
			pub_command.publish(driver_msg);  // publish command, but note that is possible that
												  // the subscriber will not receive this message.
			spincount = 0;
			while (isReady) { // Waiting for isReady to be false meaning that the driver has the new command
				ros::spinOnce();  // Allow other ROS functionallity to run
				loop_rate.sleep(); // Sleep and wake up at 1/20 second (1/SPIN_RATE) interval
				if (spincount > SPIN_RATE) {  // if isReady does not get set within 1 second re-publish
					pub_command.publish(driver_msg);
					ROS_INFO_STREAM("Just Published again driver_msg");
					spincount = 0;
				}
				spincount++;  // keep track of loop count
			}
			ROS_INFO_STREAM("waiting for rdy");  // Now wait for robot arm to reach the commanded waypoint.
			
			while(!isReady)
			{
				ros::spinOnce();
				loop_rate.sleep();
			}
			ROS_INFO_STREAM("Ready for new point");

			suction(this->srv_SetIO, srv, 0.0);
			
			driver_msg.destination=lab_invk(world[0],world[1],0.07,-90);

			//publish the point to the robot
			driver_msg.duration = 3.0;
			pub_command.publish(driver_msg);  // publish command, but note that is possible that
												  // the subscriber will not receive this message.
			spincount = 0;
			while (isReady) { // Waiting for isReady to be false meaning that the driver has the new command
				ros::spinOnce();  // Allow other ROS functionallity to run
				loop_rate.sleep(); // Sleep and wake up at 1/20 second (1/SPIN_RATE) interval
				if (spincount > SPIN_RATE) {  // if isReady does not get set within 1 second re-publish
					pub_command.publish(driver_msg);
					ROS_INFO_STREAM("Just Published again driver_msg");
					spincount = 0;
				}
				spincount++;  // keep track of loop count
			}
			ROS_INFO_STREAM("waiting for rdy");  // Now wait for robot arm to reach the commanded waypoint.
			
			while(!isReady)
			{
				ros::spinOnce();
				loop_rate.sleep();
			}
			ROS_INFO_STREAM("Ready for new point");

			rightclickdone = 1; // code finished
		} else {
			ROS_INFO_STREAM("Previous Right Click not finshed, IGNORING this Click"); 
		}
	}
}



// }