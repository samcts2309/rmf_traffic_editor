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

#include "move_tag.h"

MoveTagCommand::MoveTagCommand(
  Building* building,
  int level_idx,
  int mouse_tag_idx)
{
  _building = building;
  _to_move = _building->levels[level_idx].tags[mouse_tag_idx];
  _level_idx = level_idx;
  has_moved = false;
}

MoveTagCommand::~MoveTagCommand()
{

}

void MoveTagCommand::set_final_destination(double x, double y)
{
  _x = x;
  _y = y;
  has_moved = true;
}

void MoveTagCommand::undo()
{
  //Use ID because in future if we want to support photoshop style selective
  //undo-redos it will be consistent even after deletion of intermediate vertices.
  for (Tag& tag: _building->levels[_level_idx].tags)
  {
    if (tag.uuid == _to_move.uuid)
    {
      tag.x = _to_move.x;
      tag.y = _to_move.y;
    }
  }
}

void MoveTagCommand::redo()
{
  //Use ID because in future if we want to support photoshop style selective
  //undo-redos it will be consistent even after deletion of intermediate vertices.
  for (Tag& tag: _building->levels[_level_idx].tags)
  {
    if (tag.uuid == _to_move.uuid)
    {
      tag.x = _x;
      tag.y = _y;
    }
  }
}
