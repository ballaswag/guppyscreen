#include "image_label.h"
#include <cstring>
#include <cstdio>

// ImageLabel::ImageLabel(lv_obj_t *parent, const void *img, const char *value)
//   : ImageLabel(parent, img, v, 100)
// {
//   lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
//   lv_obj_set_size(cont, LV_PCT(45), LV_PCT(20));
//   lv_obj_set_style_border_width(cont, 2, 0);

//   lv_img_set_src(image, img);
//   lv_label_set_text(label, value);

//   lv_obj_align(image, LV_ALIGN_LEFT_MID, -30, 0);
//   lv_obj_align(label, LV_ALIGN_RIGHT_MID, 0, 0);
// }

ImageLabel::ImageLabel(lv_obj_t *parent,
		       const void* img,
		       int32_t width_pct,
		       int32_t height_pct,
		       const char *value)
  : cont(lv_obj_create(parent))
  , image(lv_img_create(cont))
  , label(lv_label_create(cont))
{
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(cont, LV_PCT(width_pct), LV_PCT(height_pct));
  lv_obj_set_style_border_width(cont, 2, 0);
  lv_obj_set_style_radius(cont, 4, 0);
  lv_obj_set_style_pad_left(cont, 5, 0);
  lv_obj_set_style_pad_right(cont, 5, 0);

  lv_img_set_src(image, img);
  lv_label_set_text(label, value);

  lv_obj_align(image, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_align(label, LV_ALIGN_RIGHT_MID, 0, 0);
}

ImageLabel::ImageLabel(lv_obj_t *parent,
		       const void* img,
		       uint16_t img_scale,
		       int32_t width_pct,
		       int32_t height_pct,
		       const char *v)
  : ImageLabel(parent, img, width_pct, height_pct, v)
{
  auto wscale = lv_disp_get_physical_hor_res(NULL) / 800.0;
  lv_img_set_size_mode(image, LV_IMG_SIZE_MODE_REAL);
  lv_img_set_zoom(image, img_scale * wscale);
}

ImageLabel::ImageLabel(lv_obj_t *parent,
		       const void* img,
		       uint16_t img_scale,
		       const char *v)
  : ImageLabel(parent, img, img_scale, 45, 20, v)
{
}

ImageLabel::~ImageLabel() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

lv_obj_t *ImageLabel::get_container() {
  return cont;
}

void ImageLabel::update_label(const char *v) {
  if (std::strcmp(v, lv_label_get_text(label)) != 0) {
    lv_label_set_text(label, v);
  }
}
