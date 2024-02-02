#ifndef __TMC_STATUS_CONTAINER_H__
#define __TMC_STATUS_CONTAINER_H__

#include "websocket_client.h"
#include "spinbox_selector.h"
#include "lvgl/lvgl.h"
#include "hv/json.hpp"

#include <string>

using json = nlohmann::json;

class TmcStatusContainer {
 public:
  TmcStatusContainer(KWebSocketClient &c,
		     lv_obj_t *parent,
		     const std::string &s);
  ~TmcStatusContainer();

  void update(json &d);

  void update_tmc_value(const std::string &stepper_name,
			const std::string &field_name,
			int value);
 private:
  KWebSocketClient &ws;
  lv_obj_t *cont;
  lv_obj_t* chart_cont;  
  lv_obj_t *label;
  lv_obj_t *legend;
  lv_obj_t *chart;
  lv_chart_series_t *sg_series;
  lv_chart_series_t *irms_series;
  lv_chart_series_t *semin_series;
  lv_chart_series_t *semax_series;
  lv_obj_t *stepper_config;

  SpinBoxSelector semin_sb;
  SpinBoxSelector semax_sb;
  SpinBoxSelector seup_sb;
  SpinBoxSelector sedn_sb;

  SpinBoxSelector toff_sb;
  SpinBoxSelector tbl_sb;
  SpinBoxSelector hstrt_sb;
  SpinBoxSelector hend_sb;
  
};

#endif // __TMC_STATUS_CONTAINER_H__
