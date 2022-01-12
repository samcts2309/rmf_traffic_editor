#ifndef TAG_H
#define TAG_H

#include <QUuid>
#include <map>
#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>

#include <QColor>

#include "coordinate_system.h"
#include "param.h"

class QGraphicsScene;


class Tag
{
public:
  double x;
  double y;
  std::string name;

  bool selected;

  QUuid uuid;
  std::map<std::string, Param> params;

  Tag();
  Tag(double _x, double _y, const std::string& _name = std::string());

  void from_yaml(const YAML::Node& data);
  YAML::Node to_yaml() const;

  void set_param(const std::string& name, const std::string& value);

  void draw(
    QGraphicsScene* scene,
    const double radius,
    const QFont& font,
    const CoordinateSystem& coordinate_system) const;

  bool is_april_tag() const;
  bool is_signage() const;

  ////////////////////////////////////////////////////////////
  static const std::vector<std::pair<std::string, Param::Type>> allowed_params;
};

#endif // TAG_H
