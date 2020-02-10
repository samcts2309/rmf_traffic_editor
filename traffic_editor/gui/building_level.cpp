/*
 * Copyright (C) 2019-2020 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
*/

#include <algorithm>

#include <QGraphicsOpacityEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QImage>
#include <QImageReader>

#include "building_level.h"
using std::string;
using std::vector;


BuildingLevel::BuildingLevel()
: Level()
{
}

BuildingLevel::~BuildingLevel()
{
}

bool BuildingLevel::from_yaml(
    const std::string &_name,
    const YAML::Node &_data)
{
  printf("parsing level [%s]\n", _name.c_str());
  name = _name;

  if (!_data.IsMap())
    throw std::runtime_error("level " + name + " YAML invalid");

  if (_data["drawing"] && _data["drawing"].IsMap())
  {
    const YAML::Node &drawing_data = _data["drawing"];
    if (!drawing_data["filename"])
      throw std::runtime_error("level " + name + " drawing invalid");
    drawing_filename = drawing_data["filename"].as<string>();

    printf("  level %s drawing: %s\n",
        name.c_str(),
        drawing_filename.c_str());

    QString qfilename = QString::fromStdString(drawing_filename);

    QImageReader image_reader(qfilename);
    image_reader.setAutoTransform(true);
    QImage image = image_reader.read();
    if (image.isNull())
    {
      qWarning("unable to read %s: %s",
          qUtf8Printable(qfilename),
          qUtf8Printable(image_reader.errorString()));
      return false;
    }
    image = image.convertToFormat(QImage::Format_Grayscale8);
    floorplan_pixmap = QPixmap::fromImage(image);
    drawing_width = floorplan_pixmap.width();
    drawing_height = floorplan_pixmap.height();
  }
  else if (_data["x_meters"] && _data["y_meters"])
  {
    x_meters = _data["x_meters"].as<double>();
    y_meters = _data["y_meters"].as<double>();
    drawing_meters_per_pixel = 0.05;  // something reasonable
    drawing_width = x_meters / drawing_meters_per_pixel;
    drawing_height = y_meters / drawing_meters_per_pixel;
  }
  else
  {
    x_meters = 100.0;
    y_meters = 100.0;
    drawing_meters_per_pixel = 0.05;
    drawing_width = x_meters / drawing_meters_per_pixel;
    drawing_height = y_meters / drawing_meters_per_pixel;
  }

  parse_vertices(_data);

  if (_data["fiducials"] && _data["fiducials"].IsSequence())
  {
    const YAML::Node &fy = _data["fiducials"];
    for (YAML::const_iterator it = fy.begin(); it != fy.end(); ++it)
    {
      Fiducial f;
      f.from_yaml(*it);
      fiducials.push_back(f);
    }
  }

  load_yaml_edge_sequence(_data, "lanes", Edge::LANE);
  load_yaml_edge_sequence(_data, "walls", Edge::WALL);
  load_yaml_edge_sequence(_data, "measurements", Edge::MEAS);
  load_yaml_edge_sequence(_data, "doors", Edge::DOOR);

  if (_data["models"] && _data["models"].IsSequence())
  {
    const YAML::Node &ys = _data["models"];
    for (YAML::const_iterator it = ys.begin(); it != ys.end(); ++it)
    {
      Model m;
      m.from_yaml(*it);
      models.push_back(m);
    }
  }

  if (_data["floors"] && _data["floors"].IsSequence())
  {
    const YAML::Node &yf = _data["floors"];
    for (YAML::const_iterator it = yf.begin(); it != yf.end(); ++it)
    {
      Polygon p;
      p.from_yaml(*it, Polygon::FLOOR);
      polygons.push_back(p);
    }
  }

  if (_data["elevation"])
    elevation = _data["elevation"].as<double>();

  if (_data["layers"] && _data["layers"].IsMap())
  {
    const YAML::Node& yl = _data["layers"];
    for (YAML::const_iterator it = yl.begin(); it != yl.end(); ++it)
    {
      Layer layer;
      layer.from_yaml(it->first.as<string>(), it->second);
      layers.push_back(layer);
    }
  }

  calculate_scale();
  return true;
}

YAML::Node BuildingLevel::to_yaml() const
{
  YAML::Node y;
  if (!drawing_filename.empty())
  {
    YAML::Node drawing_node;
    drawing_node["filename"] = drawing_filename;
    y["drawing"] = drawing_node;
  }
  else
  {
    y["x_meters"] = x_meters;
    y["y_meters"] = y_meters;
  }
  y["elevation"] = elevation;

  for (const auto &v : vertices)
    y["vertices"].push_back(v.to_yaml());

  for (const auto &f : fiducials)
    y["fiducials"].push_back(f.to_yaml());

  for (const auto &edge : edges)
  {
    YAML::Node n(edge.to_yaml());
    std::string dict_name = "unknown";
    switch (edge.type)
    {
      case Edge::LANE:
        dict_name = "lanes";
        break;
      case Edge::WALL:
        dict_name = "walls";
        break;
      case Edge::MEAS:
        dict_name = "measurements";
        break;
      case Edge::DOOR:
        dict_name = "doors";
        break;
      default:
        printf("tried to save unknown edge type: %d\n",
            static_cast<int>(edge.type));
        break;
    }
    y[dict_name].push_back(n);
  }

  for (const auto& model : models)
    y["models"].push_back(model.to_yaml());

  for (const auto& polygon : polygons)
  {
    switch(polygon.type)
    {
      case Polygon::FLOOR:
        y["floors"].push_back(polygon.to_yaml());
        break;
      default:
        printf("tried to save an unknown polygon type: %d\n",
            static_cast<int>(polygon.type));
        break;
    }
  }

  y["layers"] = YAML::Node(YAML::NodeType::Map);
  for (const auto& layer : layers)
    y["layers"][layer.name] = layer.to_yaml();

  return y;
}

bool BuildingLevel::delete_selected()
{
  edges.erase(
      std::remove_if(
          edges.begin(),
          edges.end(),
          [](Edge edge) { return edge.selected; }),
      edges.end());

  models.erase(
      std::remove_if(
          models.begin(),
          models.end(),
          [](Model model) { return model.selected; }),
      models.end());

  fiducials.erase(
      std::remove_if(
          fiducials.begin(),
          fiducials.end(),
          [](Fiducial fiducial) { return fiducial.selected; }),
      fiducials.end());

  // Vertices take a lot more care, because we have to check if a vertex
  // is used in an edge or a polygon before deleting it, and update all
  // higher-index vertex indices in the edges and polygon vertex lists.
  // Since this is a potentially expensive operation, first we'll spin
  // through the vertex list and see if any vertices are selected, and
  // only then make a copy of the vertex list.
  int selected_vertex_idx = -1;
  for (int i = 0; i < static_cast<int>(vertices.size()); i++)
    if (vertices[i].selected)
    {
      selected_vertex_idx = i;
      break;  // just grab the index of the first selected vertex
    }
  if (selected_vertex_idx >= 0)
  {
    // See if this vertex is used in any edges/polygons.
    bool vertex_used = false;
    for (const auto& edge : edges)
      if (edge.start_idx == selected_vertex_idx ||
          edge.end_idx == selected_vertex_idx)
        vertex_used = true;
    for (const auto& polygon : polygons)
      for (const int& vertex_idx : polygon.vertices)
        if (vertex_idx == selected_vertex_idx)
          vertex_used = true;
    if (vertex_used)
      return false;  // don't try to delete a vertex used in a shape

    // the vertex is not currently being used, so let's erase it
    vertices.erase(vertices.begin() + selected_vertex_idx);

    // now go through all edges and polygons to decrement any larger indices
    for (Edge& edge : edges)
    {
      if (edge.start_idx > selected_vertex_idx)
        edge.start_idx--;
      if (edge.end_idx > selected_vertex_idx)
        edge.end_idx--;
    }

    for (Polygon& polygon : polygons)
      for (int i = 0; i < static_cast<int>(polygon.vertices.size()); i++)
        if (polygon.vertices[i] > selected_vertex_idx)
          polygon.vertices[i]--;
  }
  return true;
}

void BuildingLevel::calculate_scale()
{
  // for now, just calculate the mean of the scale estimates
  double scale_sum = 0.0;
  int scale_count = 0;

  for (auto &edge : edges)
  {
    if (edge.type == Edge::MEAS)
    {
      scale_count++;
      const double dx = vertices[edge.start_idx].x - vertices[edge.end_idx].x;
      const double dy = vertices[edge.start_idx].y - vertices[edge.end_idx].y;
      const double distance_pixels = sqrt(dx*dx + dy*dy);
      // todo: a clean, strongly-typed parameter API for edges
      const double distance_meters =
          edge.params[std::string("distance")].value_double;
      scale_sum += distance_meters / distance_pixels;
    }
  }

  if (scale_count > 0)
  {
    drawing_meters_per_pixel = scale_sum / static_cast<double>(scale_count);
    printf("used %d measurements to estimate meters/pixel as %.5f\n",
        scale_count, drawing_meters_per_pixel);
  }
  else
    drawing_meters_per_pixel = 0.05;  // default to something reasonable

  if (drawing_width && drawing_height && drawing_meters_per_pixel > 0.0)
  {
    x_meters = drawing_width * drawing_meters_per_pixel;
    y_meters = drawing_height * drawing_meters_per_pixel;
  }
}

void BuildingLevel::draw_lane(QGraphicsScene *scene, const Edge &edge) const
{
  const auto &v_start = vertices[edge.start_idx];
  const auto &v_end = vertices[edge.end_idx];
  const double dx = v_end.x - v_start.x;
  const double dy = v_end.y - v_start.y;
  const double len = sqrt(dx*dx + dy*dy);

  const double lane_pen_width = 1.0 / drawing_meters_per_pixel;

  const QPen arrow_pen(
      QBrush(QColor::fromRgbF(0.0, 0.0, 0.0, 0.5)),
      lane_pen_width / 8);

  // dimensions for the direction indicators along this path
  const double arrow_w = lane_pen_width / 2.5;  // width of arrowheads
  const double arrow_l = lane_pen_width / 2.5;  // length of arrowheads
  const double arrow_spacing = lane_pen_width / 2.0;

  const double norm_x = dx / len;
  const double norm_y = dy / len;

  for (double d = 0.0; d < len; d += arrow_spacing)
  {
    // first calculate the center vertex of this arrowhead
    const double cx = v_start.x + d * norm_x;
    const double cy = v_start.y + d * norm_y;
    // one edge vertex of arrowhead
    const double e1x = cx - arrow_w * norm_y;
    const double e1y = cy + arrow_w * norm_x;
    // another edge vertex of arrowhead
    const double e2x = cx + arrow_w * norm_y;
    const double e2y = cy - arrow_w * norm_x;
    // tip of arrowhead
    const double tx = cx + arrow_l * norm_x;
    const double ty = cy + arrow_l * norm_y;
    // now add arrowhead lines
    scene->addLine(e1x, e1y, tx, ty, arrow_pen);
    scene->addLine(e2x, e2y, tx, ty, arrow_pen);

    if (d > 0.0 && edge.is_bidirectional())
    {
      const double back_tx = cx - arrow_l * norm_x;
      const double back_ty = cy - arrow_l * norm_y;
      scene->addLine(e1x, e1y, back_tx, back_ty, arrow_pen);
      scene->addLine(e2x, e2y, back_tx, back_ty, arrow_pen);
    }
  }

  QColor color;
  switch (edge.get_graph_idx())
  {
    case 0: color.setRgbF(0.0, 0.5, 0.0); break;
    case 1: color.setRgbF(0.0, 0.0, 0.5); break;
    case 2: color.setRgbF(0.0, 0.5, 0.5); break;
    case 3: color.setRgbF(0.5, 0.5, 0.0); break;
    case 4: color.setRgbF(0.5, 0.0, 0.5); break;
    case 5: color.setRgbF(0.5, 0.5, 0.5); break;
    default: break;  // will render as dark grey
  }

  // always draw lane as red if it's selected
  if (edge.selected)
    color.setRgbF(0.5, 0.0, 0.0);

  // always draw lanes somewhat transparent
  color.setAlphaF(0.5);

  scene->addLine(
      v_start.x, v_start.y,
      v_end.x, v_end.y,
      QPen(QBrush(color), lane_pen_width, Qt::SolidLine, Qt::RoundCap));

  // draw the orientation icon, if specified
  auto orientation_it = edge.params.find("orientation");
  if (orientation_it != edge.params.end()) {
    // draw robot-outline box midway down this lane
    const double mx = (v_start.x + v_end.x) / 2.0;
    const double my = (v_start.y + v_end.y) / 2.0;
    const double yaw = atan2(norm_y, norm_x);

    // robot-box half-dimensions in meters
    const double rw = 0.4 / drawing_meters_per_pixel;
    const double rl = 0.5 / drawing_meters_per_pixel;

    // calculate the corners of the 'robot' box

    // front-left
    // |mx| + |cos -sin| | rl|
    // |my|   |sin  cos| | rw|
    const double flx = mx + rl * cos(yaw) - rw * sin(yaw);
    const double fly = my + rl * sin(yaw) + rw * cos(yaw);

    // front-right
    // |mx| + |cos -sin| | rl|
    // |my|   |sin  cos| |-rw|
    const double frx = mx + rl * cos(yaw) + rw * sin(yaw);
    const double fry = my + rl * sin(yaw) - rw * cos(yaw);

    // back-left
    // |mx| + |cos -sin| |-rl|
    // |my|   |sin  cos| | rw|
    const double blx = mx - rl * cos(yaw) - rw * sin(yaw);
    const double bly = my - rl * sin(yaw) + rw * cos(yaw);

    // back-right
    // |mx| + |cos -sin| |-rl|
    // |my|   |sin  cos| |-rw|
    const double brx = mx - rl * cos(yaw) + rw * sin(yaw);
    const double bry = my - rl * sin(yaw) - rw * cos(yaw);

    QPainterPath pp;
    pp.moveTo(QPointF(flx, fly));
    pp.lineTo(QPointF(frx, fry));
    pp.lineTo(QPointF(brx, bry));
    pp.lineTo(QPointF(blx, bly));
    pp.lineTo(QPointF(flx, fly));
    pp.moveTo(QPointF(mx, my));

    QPen orientation_pen(Qt::white, 5.0);
    if (orientation_it->second.value_string == "forward")
    {
      const double hix = mx + 1.0 * cos(yaw) / drawing_meters_per_pixel;
      const double hiy = my + 1.0 * sin(yaw) / drawing_meters_per_pixel;
      pp.lineTo(QPointF(hix, hiy));
      scene->addPath(pp, orientation_pen);
    }
    else if (orientation_it->second.value_string == "backward")
    {
      const double hix = mx - 1.0 * cos(yaw) / drawing_meters_per_pixel;
      const double hiy = my - 1.0 * sin(yaw) / drawing_meters_per_pixel;
      pp.lineTo(QPointF(hix, hiy));
      scene->addPath(pp, orientation_pen);
    }
  }
}

void BuildingLevel::draw_wall(QGraphicsScene *scene, const Edge &edge) const
{
  const auto &v_start = vertices[edge.start_idx];
  const auto &v_end = vertices[edge.end_idx];

  const double r = edge.selected ? 0.5 : 0.0;
  const double b = edge.selected ? 0.0 : 0.5;

  scene->addLine(
      v_start.x, v_start.y,
      v_end.x, v_end.y,
      QPen(
        QBrush(QColor::fromRgbF(r, 0.0, b, 0.5)),
        0.2 / drawing_meters_per_pixel,
        Qt::SolidLine, Qt::RoundCap));
}

void BuildingLevel::draw_meas(QGraphicsScene *scene, const Edge &edge) const
{
  const auto &v_start = vertices[edge.start_idx];
  const auto &v_end = vertices[edge.end_idx];
  const double b = edge.selected ? 0.0 : 0.5;

  scene->addLine(
      v_start.x, v_start.y,
      v_end.x, v_end.y,
      QPen(
        QBrush(QColor::fromRgbF(0.5, 0, b, 0.5)),
        0.5 / drawing_meters_per_pixel,
        Qt::SolidLine, Qt::RoundCap));
}

void BuildingLevel::draw_door(QGraphicsScene *scene, const Edge &edge) const
{
  const auto &v_start = vertices[edge.start_idx];
  const auto &v_end = vertices[edge.end_idx];
  const double g = edge.selected ? 1.0 : 0.0;
  const double door_thickness = 0.2;  // meters
  const double door_motion_thickness = 0.05;  // meters

  scene->addLine(
      v_start.x, v_start.y,
      v_end.x, v_end.y,
      QPen(
        QBrush(QColor::fromRgbF(1.0, g, 0.0, 0.5)),
        door_thickness / drawing_meters_per_pixel,
        Qt::SolidLine, Qt::RoundCap));

  auto door_axis_it = edge.params.find("motion_axis");
  std::string door_axis("start");
  if (door_axis_it != edge.params.end())
    door_axis = door_axis_it->second.value_string;

  double motion_degrees = 90;
  auto motion_degrees_it = edge.params.find("motion_degrees");
  if (motion_degrees_it != edge.params.end())
    motion_degrees = motion_degrees_it->second.value_double;

  int motion_dir = 1;
  auto motion_dir_it = edge.params.find("motion_direction");
  if (motion_dir_it != edge.params.end())
    motion_dir = motion_dir_it->second.value_int;

  QPainterPath door_motion_path;

  const double door_dx = v_end.x - v_start.x;
  const double door_dy = v_end.y - v_start.y;
  const double door_length = sqrt(door_dx * door_dx + door_dy * door_dy);
  const double door_angle = atan2(door_dy, door_dx);

  auto door_type_it = edge.params.find("type");
  if (door_type_it != edge.params.end())
  {
    const double DEG2RAD = M_PI / 180.0;

    const std::string &door_type = door_type_it->second.value_string;
    if (door_type == "hinged")
    {
      const double hinge_x = door_axis == "start" ? v_start.x : v_end.x;
      const double hinge_y = door_axis == "start" ? v_start.y : v_end.y;
      const double angle_offset = door_axis == "start" ? 0.0 : M_PI;
      
      add_door_swing_path(
          door_motion_path,
          hinge_x,
          hinge_y,
          door_length,
          door_angle + angle_offset,
          door_angle + angle_offset + DEG2RAD * motion_dir * motion_degrees);
    }
    else if (door_type == "double_hinged")
    {
      // each door section is half as long as door_length
      add_door_swing_path(
          door_motion_path,
          v_start.x,
          v_start.y,
          door_length / 2,
          door_angle,
          door_angle + DEG2RAD * motion_dir * motion_degrees);

      add_door_swing_path(
          door_motion_path,
          v_end.x,
          v_end.y,
          door_length / 2,
          door_angle + M_PI,
          door_angle + M_PI - DEG2RAD * motion_dir * motion_degrees);
    }
    else if (door_type == "sliding")
    {
      add_door_slide_path(
          door_motion_path,
          v_start.x,
          v_start.y,
          door_length,
          door_angle);
    }
    else if (door_type == "double_sliding")
    {
      // each door section is half as long as door_length
      add_door_slide_path(
          door_motion_path,
          v_start.x,
          v_start.y,
          door_length / 2,
          door_angle);
      add_door_slide_path(
          door_motion_path,
          v_end.x,
          v_end.y,
          door_length / 2,
          door_angle + M_PI);
    }
    else
    {
      printf("tried to draw unknown door type: [%s]\n", door_type.c_str());
    }
  }
  scene->addPath(
      door_motion_path,
      QPen(Qt::black, door_motion_thickness / drawing_meters_per_pixel));
}

void BuildingLevel::add_door_slide_path(
    QPainterPath &path,
    double hinge_x,
    double hinge_y,
    double door_length,
    double door_angle) const
{
  // first draw the door as a thin line
  path.moveTo(hinge_x, hinge_y);
  path.lineTo(
      hinge_x + door_length * cos(door_angle),
      hinge_y + door_length * sin(door_angle));

  // now draw a box around where it slides (in the wall, usually)
  const double th = door_angle;  // makes expressions below single-line...
  const double pi_2 = M_PI / 2.0;
  const double s = 0.15 / drawing_meters_per_pixel;  // sliding panel thickness

  const QPointF p1(
      hinge_x - s * cos(th + pi_2),
      hinge_y - s * sin(th + pi_2));

  const QPointF p2(
      hinge_x - s * cos(th + pi_2) - door_length * cos(th),
      hinge_y - s * sin(th + pi_2) - door_length * sin(th));

  const QPointF p3(
      hinge_x + s * cos(th + pi_2) - door_length * cos(th),
      hinge_y + s * sin(th + pi_2) - door_length * sin(th));

  const QPointF p4(
      hinge_x + s * cos(th + pi_2),
      hinge_y + s * sin(th + pi_2));


  path.moveTo(p1);
  path.lineTo(p2);
  path.lineTo(p3);
  path.lineTo(p4);
  path.lineTo(p1);
}

void BuildingLevel::add_door_swing_path(
    QPainterPath &path,
    double hinge_x,
    double hinge_y,
    double door_length,
    double start_angle,
    double end_angle) const
{
  path.moveTo(hinge_x, hinge_y);
  path.lineTo(
      hinge_x + door_length * cos(start_angle),
      hinge_y + door_length * sin(start_angle));

  const int NUM_MOTION_STEPS = 10;
  const double angle_inc = (end_angle - start_angle) / (NUM_MOTION_STEPS-1);
  for (int i = 0; i < NUM_MOTION_STEPS; i++)
  {
    // compute door opening angle at this motion step
    const double a = start_angle + i * angle_inc;

    path.lineTo(
        hinge_x + door_length * cos(a),
        hinge_y + door_length * sin(a));
  }

  path.lineTo(hinge_x, hinge_y);
}

void BuildingLevel::draw_polygons(QGraphicsScene *scene) const
{
  QBrush polygon_brush(QColor::fromRgbF(0.8, 0.8, 0.8, 0.5));
  QBrush selected_polygon_brush(QColor::fromRgbF(1.0, 0.0, 0.0, 0.5));

  for (const auto &polygon : polygons)
  {
    // now draw the polygons
    QVector<QPointF> polygon_vertices;
    for (const auto &vertex_idx: polygon.vertices)
    {
      const Vertex &v = vertices[vertex_idx];
      polygon_vertices.append(QPointF(v.x, v.y));
    }
    scene->addPolygon(
        QPolygonF(polygon_vertices),
        QPen(Qt::black),
        polygon.selected ? selected_polygon_brush : polygon_brush);
  }
#if 0
  // ahhhhh only for debugging...
  // plot the nearest projection point to a polygon, if it's set
  // to something nonzero
  if (level->polygon_edge_proj_x != 0) {
    const double r = 5.0;
    addEllipse(
        polygon_edge_proj_x - r,
        polygon_edge_proj_y - r,
        2 * r,
        2 * r,
        QPen(Qt::black),
        QBrush(Qt::blue));
  }
#endif
}

void BuildingLevel::clear_selection()
{
  for (auto& vertex : vertices)
    vertex.selected = false;

  for (auto& edge : edges)
    edge.selected = false;

  for (auto& model : models)
    model.selected = false;

  for (auto& polygon : polygons)
    polygon.selected = false;

  for (auto& fiducial : fiducials)
    fiducial.selected = false;
}

void BuildingLevel::draw(
    QGraphicsScene *scene,
    vector<EditorModel>& editor_models) const
{
  if (drawing_filename.size())
  {
    scene->setSceneRect(
        QRectF(0, 0, drawing_width, drawing_height));
    scene->addPixmap(floorplan_pixmap);
  }
  else
  {
    const double w = x_meters / drawing_meters_per_pixel;
    const double h = y_meters / drawing_meters_per_pixel;
    scene->setSceneRect(QRectF(0, 0, w, h));
    scene->addRect(0, 0, w, h, QPen(), Qt::white);
  }

  draw_polygons(scene);

  for (const auto& layer : layers)
  {
    if (!layer.visible)
      continue;

    //printf("floorplan height: %d\n", level.floorplan_pixmap.height());
    //printf("layer pixmap height: %d\n", layer.pixmap.height());
    QGraphicsPixmapItem *item = scene->addPixmap(layer.pixmap);
    // set the origin of the pixmap frame to the lower-left corner
    item->setOffset(0, -layer.pixmap.height());
    item->setPos(
        -layer.translation_x / drawing_meters_per_pixel,
        layer.translation_y / drawing_meters_per_pixel);
    item->setScale(layer.meters_per_pixel / drawing_meters_per_pixel);
    item->setRotation(-1.0 * layer.rotation * 180.0 / M_PI);
    QGraphicsOpacityEffect *opacity_effect = new QGraphicsOpacityEffect;
    opacity_effect->setOpacity(0.5);
    item->setGraphicsEffect(opacity_effect);
  }

  // now draw all the models
  for (const auto &model : models)
  {
    // find the pixmap we need for this model
    QPixmap pixmap;
    double model_meters_per_pixel = 1.0;  // will get overridden
    for (auto &editor_model : editor_models)
    {
      if (editor_model.name == model.model_name)
      {
        pixmap = editor_model.get_pixmap();
        model_meters_per_pixel = editor_model.meters_per_pixel;
        break;
      }
    }
    if (pixmap.isNull())
      continue;  // couldn't load the pixmap; ignore it.

    QGraphicsPixmapItem *item = scene->addPixmap(pixmap);
    item->setOffset(-pixmap.width()/2, -pixmap.height()/2);
    item->setScale(model_meters_per_pixel / drawing_meters_per_pixel);
    item->setPos(model.x, model.y);
    item->setRotation(-model.yaw * 180.0 / M_PI);

    // make the model "glow" if it is selected
    if (model.selected)
    {
      QGraphicsColorizeEffect *colorize = new QGraphicsColorizeEffect;
      colorize->setColor(QColor::fromRgbF(1.0, 0.2, 0.0, 1.0));
      colorize->setStrength(1.0);
      item->setGraphicsEffect(colorize);
    }
  }

  for (const auto &edge : edges)
  {
    switch (edge.type)
    {
      case Edge::LANE: draw_lane(scene, edge); break;
      case Edge::WALL: draw_wall(scene, edge); break;
      case Edge::MEAS: draw_meas(scene, edge); break;
      case Edge::DOOR: draw_door(scene, edge); break;
      default:
        printf("tried to draw unknown edge type: %d\n",
            static_cast<int>(edge.type));
        break;
    }
  }

  for (const auto &v : vertices)
    v.draw(scene, 0.1 / drawing_meters_per_pixel);

  for (const auto &f : fiducials)
    f.draw(scene, drawing_meters_per_pixel);
}
