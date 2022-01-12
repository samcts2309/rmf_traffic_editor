#include <cmath>

#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QIcon>

#include "tag.h"
using std::string;
using std::vector;
using std::pair;

const vector<pair<string, Param::Type>> Tag::allowed_params
{
  { "is_april_tag", Param::Type::BOOL },
  { "is_signage", Param::Type::BOOL},
  { "human_goal_set_name", Param::Type::STRING },
};


Tag::Tag()
: x(0), y(0), selected(false)
{
  uuid = QUuid::createUuid();
}

Tag::Tag(double _x, double _y, const string& _name)
: x(_x), y(_y), name(_name), selected(false)
{
  uuid = QUuid::createUuid();
}

void Tag::from_yaml(const YAML::Node& data)
{
  if (!data.IsSequence())
    throw std::runtime_error("Tag::from_yaml expected a sequence");
  x = data[0].as<double>();
  y = data[1].as<double>();
  if (data.size() < 4)
    return;// todo: remove... intended only during format transition
  // skip the z-offset in data[2] for now
  name = data[3].as<string>();

  // load the parameters, all of which (including the params map) are
  // optional at the moment.
  if (data.size() >= 4)
  {
    for (YAML::const_iterator it = data[4].begin(); it != data[4].end(); ++it)
    {
      Param p;
      p.from_yaml(it->second);
      params[it->first.as<string>()] = p;
    }
  }
}

YAML::Node Tag::to_yaml() const
{
  // This is in image space. I think it's safe to say nobody is clicking
  // with more than 1/1000 precision inside a single pixel.

  YAML::Node tag_node;
  tag_node.SetStyle(YAML::EmitterStyle::Flow);
  tag_node.push_back(std::round(x * 1000.0) / 1000.0);
  tag_node.push_back(std::round(y * 1000.0) / 1000.0);
  tag_node.push_back(0.0);  // placeholder for Z offsets in the future
  tag_node.push_back(name);

  if (!params.empty())
  {
    YAML::Node params_node(YAML::NodeType::Map);
    for (const auto& param : params)
      params_node[param.first] = param.second.to_yaml();
    tag_node.push_back(params_node);
  }
  return tag_node;
}

void Tag::draw(
  QGraphicsScene* scene,
  const double radius,
  const QFont& font,
  const CoordinateSystem& coordinate_system) const
{
  QPen vertex_pen(Qt::black);
  vertex_pen.setWidthF(radius / 2.0);

  const double a = 0.5;

  const QColor vertex_color = QColor::fromRgbF(0.0, 0.0, 0.5);
  QColor nonselected_color(vertex_color);
  nonselected_color.setAlphaF(a);

  QColor selected_color = QColor::fromRgbF(1.0, 0.0, 0.0, a);

  const QBrush vertex_brush =
    selected ? QBrush(selected_color) : QBrush(nonselected_color);

  QGraphicsEllipseItem* ellipse_item = scene->addEllipse(
    x - radius,
    y - radius,
    2 * radius,
    2 * radius,
    vertex_pen,
    vertex_brush);
  ellipse_item->setZValue(20.0);  // above all lane/wall edges

  // add some icons depending on the superpowers of this vertex
  QPen annotation_pen(Qt::black);
  annotation_pen.setWidthF(radius / 4.0);
  const double icon_ring_radius = radius * 2.5;
  const double icon_scale = 2.0 * radius / 128.0;

  if (is_april_tag())
  {
    const double icon_bearing = -135.0 * M_PI / 180.0;
    QIcon icon(":icons/aprialtag.svg");
    QPixmap pixmap(icon.pixmap(icon.actualSize(QSize(128, 128))));
    QGraphicsPixmapItem* pixmap_item = scene->addPixmap(pixmap);
    pixmap_item->setOffset(
      -pixmap.width() / 2,
      -pixmap.height() / 2);
    pixmap_item->setScale(icon_scale);
    pixmap_item->setZValue(20.0);
    pixmap_item->setPos(
      x + icon_ring_radius * cos(icon_bearing),
      y - icon_ring_radius * sin(icon_bearing));
    if (!coordinate_system.is_y_flipped())
      pixmap_item->setTransform(pixmap_item->transform().scale(1, -1));
    pixmap_item->setToolTip("This tag is a april tag");
  }

  if (is_signage())
  {
    const double icon_bearing = -135.0 * M_PI / 180.0;
    QIcon icon(":icons/signage.svg");
    QPixmap pixmap(icon.pixmap(icon.actualSize(QSize(128, 128))));
    QGraphicsPixmapItem* pixmap_item = scene->addPixmap(pixmap);
    pixmap_item->setOffset(
      -pixmap.width() / 2,
      -pixmap.height() / 2);
    pixmap_item->setScale(icon_scale);
    pixmap_item->setZValue(20.0);
    pixmap_item->setPos(
      x + icon_ring_radius * cos(icon_bearing),
      y - icon_ring_radius * sin(icon_bearing));
    if (!coordinate_system.is_y_flipped())
      pixmap_item->setTransform(pixmap_item->transform().scale(1, -1));
    pixmap_item->setToolTip("This tag is a signage");
  }

  if (!name.empty())
  {
    QGraphicsSimpleTextItem* text_item = scene->addSimpleText(
      QString::fromStdString(name),
      font);
    text_item->setBrush(selected ? selected_color : vertex_color);

    if (coordinate_system.is_y_flipped())
    {
      // default screen coordinates: +Y=down. Nothing special needed.
      text_item->setPos(x, y - 1 + radius);
    }
    else
    {
      // if Y is not flipped, this means we have +Y=up, so we have to
      // flip the text, because Qt's default is for +Y=down screen coords
      text_item->setTransform(QTransform::fromScale(1.0, -1.0));
      text_item->setPos(x, y + 1 + radius);
    }
  }
}

void Tag::set_param(const std::string& param_name, const std::string& value)
{
  auto it = params.find(param_name);
  if (it == params.end())
  {
    printf("tried to set unknown parameter [%s]\n", param_name.c_str());
    return;  // unknown parameter
  }
  it->second.set(value);
}

bool Tag::is_april_tag() const
{
  const auto it = params.find("is_april_tag");
  if (it == params.end())
    return false;

  return it->second.value_bool;
}

bool Tag::is_signage() const
{
  const auto it = params.find("is_signage");
  if (it == params.end())
    return false;

  return it->second.value_bool;
}
