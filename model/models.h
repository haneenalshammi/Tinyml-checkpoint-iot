/* models.h — auto-generated
 * Returns: 0=safe | 1=danger -> save checkpoint
 */
#pragma once
#include "energy_predictor.h"

static inline float
predict_failure(float energy_level,
                float energy_slope,
                float sends_in_session)
{
  float raw[3] = {energy_level, energy_slope, sends_in_session};
  float mn[3]  = {33.606869f, -0.059623f, 17.907775f};
  float sd[3]  = {17.064212f, 10.202924f, 20.731111f};
  float norm[3];
  for(int i=0; i<3; i++) norm[i]=(raw[i]-mn[i])/sd[i];
  return energy_pred_predict(norm, 3);
}
