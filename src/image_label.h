#ifndef __IMAGE_LABEL_H__
#define __IMAGE_LABEL_H__

#include "lvgl/lvgl.h"

class ImageLabel {
 public:
  ImageLabel(lv_obj_t *parent,
	     const void* img,
	     int32_t width_pct,
	     int32_t height_pct,
	     const char *v);
  ImageLabel(lv_obj_t *parent,
	     const void* img,
	     uint16_t img_scale,
	     int32_t width_pct,
	     int32_t height_pct,
	     const char *v);
  ImageLabel(lv_obj_t *parent,
	     const void* img,
	     uint16_t img_scale,
	     const char *v);
   
  
  ~ImageLabel();

  lv_obj_t *get_container();
  void update_label(const char* value);

 private:
  lv_obj_t *cont;
  lv_obj_t *image;
  lv_obj_t *label;
  
};

#endif // __IMAGE_LABEL_H__
