#include <uWS/uWS.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "helpers.h"
#include "json.hpp"
#include "spline.h"

// for convenience
using nlohmann::json;
using std::string;
using std::vector;
using namespace std;


int main() {
  uWS::Hub h;

  // Load up map values for waypoint's x,y,s and d normalized normal vectors
  vector<double> map_waypoints_x;
  vector<double> map_waypoints_y;
  vector<double> map_waypoints_s;
  vector<double> map_waypoints_dx;
  vector<double> map_waypoints_dy;

  // Waypoint map to read from
  string map_file_ = "../data/highway_map.csv";
  // The max s value before wrapping around the track back to 0
  double max_s = 6945.554;

  std::ifstream in_map_(map_file_.c_str(), std::ifstream::in);

  string line;
  while (getline(in_map_, line)) {
    std::istringstream iss(line);
    double x;
    double y;
    float s;
    float d_x;
    float d_y;
    iss >> x;
    iss >> y;
    iss >> s;
    iss >> d_x;
    iss >> d_y;
    map_waypoints_x.push_back(x);
    map_waypoints_y.push_back(y);
    map_waypoints_s.push_back(s);
    map_waypoints_dx.push_back(d_x);
    map_waypoints_dy.push_back(d_y);
  }

  h.onMessage([&map_waypoints_x,&map_waypoints_y,&map_waypoints_s,
               &map_waypoints_dx,&map_waypoints_dy]
              (uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
               uWS::OpCode opCode) {

  // allocate Lane marking as per specification
  // Start from user defined lane 
  int lane_id = 1;
  //width of the lane in meters 
  double lane_width = 4.0;
  //keep a reference velocity to ego vehicle mhr
  double max_vel = 50.0; //mph
  double desired_speed = max_vel - 2.0; //mph -> desired velocity is always lesser than the max velocity.
  double ref_vel = desired_speed; //mph -> reference velocity always percept based on sensor fusion data.
  //double max_ref_vel = 49;
  //double min_ref_vel = 3; 
  int dist_to_maintain = 30;
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    if (length && length > 2 && data[0] == '4' && data[1] == '2') {

      auto s = hasData(data);

      if (s != "") {
        auto j = json::parse(s);
        
        string event = j[0].get<string>();
        
        if (event == "telemetry") {
          // j[1] is the data JSON object
          
          // Main car's localization Data
          double car_x = j[1]["x"];
          double car_y = j[1]["y"];
          double car_s = j[1]["s"];
          double car_d = j[1]["d"];
          double car_yaw = j[1]["yaw"];
          double car_speed = j[1]["speed"];

          // Previous path data given to the Planner
          auto previous_path_x = j[1]["previous_path_x"];
          auto previous_path_y = j[1]["previous_path_y"];
          // Previous path's end s and d values 
          double end_path_s = j[1]["end_path_s"];
          double end_path_d = j[1]["end_path_d"];

          // Sensor Fusion Data, a list of all other cars on the same side 
          //   of the road.
          auto sensor_fusion = j[1]["sensor_fusion"];

          
          json msgJson;

          vector<double> next_x_vals;
          vector<double> next_y_vals;

          /**
           * TODO: define a path made up of (x,y) points that the car will visit
           *   sequentially every .02 seconds
           */
           int prev_size = previous_path_x.size();
          if (prev_size >0) {
            car_s = end_path_s;
          }
          bool pretty_close = false;
          bool change_lane = false;

          //Localization to predict all other car into the same lane
          for (int i = 0; i < sensor_fusion.size(); i++) {
            double vx = sensor_fusion[i][3];
            double vy = sensor_fusion[i][4];
            double check_speed = sqrt(vx*vx+vy*vy);
            double check_car_s = sensor_fusion[i][5];
            float check_car_d = sensor_fusion[i][6];
            double gap_dist = check_car_s - car_s;

            if(check_car_d < (2+4*lane_id+2) && check_car_d > (2+4*lane_id-2)) {
              check_car_s += ((double)prev_size*0.02*check_speed); 
              if((check_car_s > car_s) && (gap_dist < dist_to_maintain)) {
            //for lowe reference velocity we dont want to hit the car in front of us
            //could also try to change lane with ref_velocity
                cout << "check car is in same lane and gap is " << gap_dist << endl;
                pretty_close = true;
                change_lane = true;
              } // end of gap
            } //end of check_car_d
          } // end of sensor fusion loop

          int shift_lane_by = 0;
          if (change_lane) {
            cout << "preparing for lane change" << endl;
            vector<double> lc = lane_to_shift(sensor_fusion, lane_id, car_s, car_d, ref_vel);
            shift_lane_by = lc[0];
            cout << "shift lane by " << shift_lane_by << endl;
            lane_id += shift_lane_by;
            ref_vel +=lc[1];
            cout << "new ref_vel after reduction by " << lc[1] << " is, ref_vel=" << ref_vel << endl;
          }
          if (pretty_close && shift_lane_by == 0) {
              ref_vel -= 0.224;
          }
          else if(ref_vel < desired_speed) {
              ref_vel += 0.224;
          }
          //create a widely spread x,y waypoint spaces at 30m
          //later we will interpolate this point using spline and fit it with more point 
          //that control speed.
          //END of TODO

          msgJson["next_x"] = next_x_vals;
          msgJson["next_y"] = next_y_vals;

          auto msg = "42[\"control\","+ msgJson.dump()+"]";

          ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
        }  // end "telemetry" if
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }  // end websocket if
  }); // end h.onMessage

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  
  h.run();
}