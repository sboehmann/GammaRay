/*
  enumsextension.cpp

  This file is part of GammaRay, the Qt application inspection and
  manipulation tool.

  Copyright (C) 2014 Klarälvdalens Datakonsult AB, a KDAB Group company, info@kdab.com
  Author: Anton Kreuzkamp <anton.kreuzkamp@kdab.com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "enumsextension.h"
#include "connectionfilterproxymodel.h"
#include "propertycontroller.h"
#include "probe.h"
#include <objectenummodel.h>
#include <QMetaProperty>

using namespace GammaRay;

EnumsExtension::EnumsExtension(PropertyController* controller) :
  PropertyControllerExtension(controller->objectBaseName() + ".enums"),
  m_model(new ObjectEnumModel(controller))
{
  controller->registerModel(m_model, "enums");
}

EnumsExtension::~EnumsExtension()
{
}

bool EnumsExtension::setObject(QObject* object)
{
  m_model->setMetaObject(object ? object->metaObject() : 0);
  return true;
}

bool EnumsExtension::setObject(void* object, const QString& typeName)
{
  Q_UNUSED(object)
  Q_UNUSED(typeName)
  return false;
}

bool EnumsExtension::setMetaObject(const QMetaObject* metaObject)
{
  m_model->setMetaObject(metaObject);
  return true;
}
