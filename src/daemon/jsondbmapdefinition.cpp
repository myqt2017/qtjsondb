/****************************************************************************
**
** Copyright (C) 2012 Nokia Corporation and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/
**
** This file is part of the QtAddOn.JsonDb module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** GNU Lesser General Public License Usage
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this
** file. Please review the following information to ensure the GNU Lesser
** General Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU General
** Public License version 3.0 as published by the Free Software Foundation
** and appearing in the file LICENSE.GPL included in the packaging of this
** file. Please review the following information to ensure the GNU General
** Public License version 3.0 requirements will be met:
** http://www.gnu.org/copyleft/gpl.html.
**
** Other Usage
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
**
**
**
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include <QDebug>
#include <QElapsedTimer>
#include <QRegExp>
#include <QJSValue>
#include <QJSValueIterator>
#include <QStringList>

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#include "jsondbpartition.h"
#include "jsondb-strings.h"
#include "jsondb-error.h"
#include "json.h"

#include "jsondbproxy.h"
#include "jsondbobjecttable.h"
#include "jsondbmapdefinition.h"
#include "jsondbsettings.h"
#include "jsondbview.h"

QT_BEGIN_NAMESPACE_JSONDB

JsonDbMapDefinition::JsonDbMapDefinition(const JsonDbOwner *owner, JsonDbPartition *partition, QJsonObject definition, QObject *parent) :
    QObject(parent)
    , mPartition(partition)
    , mOwner(owner)
    , mDefinition(definition)
    , mScriptEngine(0)
    , mUuid(definition.value(JsonDbString::kUuidStr).toString())
    , mTargetType(definition.value("targetType").toString())
    , mTargetTable(mPartition->findObjectTable(mTargetType))
{
    QJsonObject sourceFunctions(mDefinition.contains("join")
                                ? mDefinition.value("join").toObject()
                                : mDefinition.value("map").toObject());
    mSourceTypes = sourceFunctions.keys();
    for (int i = 0; i < mSourceTypes.size(); i++) {
        const QString &sourceType = mSourceTypes[i];
        mSourceTables[sourceType] = mPartition->findObjectTable(sourceType);
    }
}

void JsonDbMapDefinition::definitionCreated()
{
    initScriptEngine();
    initIndexes();

    foreach (const QString &sourceType, mSourceTypes) {
        GetObjectsResult getObjectResponse = mPartition->getObjects(JsonDbString::kTypeStr, sourceType);
        if (!getObjectResponse.error.isNull()) {
            if (jsondbSettings->verbose())
                qDebug() << "createMapDefinition" << mSourceTypes << sourceType << mTargetType << getObjectResponse.error.toString();
            setError(getObjectResponse.error.toString());
            return;
        }
        JsonDbObjectList objects = getObjectResponse.data;
        bool isJoin = mDefinition.contains(QLatin1String("join"));
        for (int i = 0; i < objects.size(); i++) {
            JsonDbObject object(objects.at(i));
            if (isJoin)
                unmapObject(object);
            updateObject(JsonDbObject(), objects.at(i));
        }
    }
}

void JsonDbMapDefinition::definitionRemoved(JsonDbPartition *partition, JsonDbObjectTable *table, const QString targetType, const QString &definitionUuid)
{
    // remove the output objects
    GetObjectsResult getObjectResponse = table->getObjects(QLatin1String("_sourceUuids.*"),
                                                           definitionUuid,
                                                           targetType);
    JsonDbObjectList objects = getObjectResponse.data;
    for (int i = 0; i < objects.size(); i++) {
        JsonDbObject o = objects[i];
        o.markDeleted();
        partition->updateObject(partition->defaultOwner(), o, JsonDbPartition::ViewObject);
    }
}

void JsonDbMapDefinition::initScriptEngine()
{
    if (mScriptEngine)
        return;

    mScriptEngine = new QJSEngine(this);
    QString message;
    bool compiled = compileMapFunctions(mScriptEngine, mDefinition, mMapFunctions, message);
    if (!compiled)
      setError(message);
    QJSValue globalObject = mScriptEngine->globalObject();
    globalObject.setProperty("console", mScriptEngine->newQObject(new Console()));

    mJoinProxy = new JsonDbJoinProxy(mOwner, mPartition, this);
    connect(mJoinProxy, SIGNAL(lookupRequested(QJSValue,QJSValue)),
            this, SLOT(lookupRequested(QJSValue,QJSValue)));
    connect(mJoinProxy, SIGNAL(viewObjectEmitted(QJSValue)),
            this, SLOT(viewObjectEmitted(QJSValue)));
    globalObject.setProperty("_jsondb", mScriptEngine->newQObject(mJoinProxy));
    // we use this snippet of javascript so that we can bind "jsondb.emit"
    // even though "emit" is a Qt keyword
    if (mDefinition.contains("join"))
        // only joins can use lookup()
        mScriptEngine->evaluate("var jsondb = { emit: _jsondb.create, lookup: _jsondb.lookup, createUuidFromString: _jsondb.createUuidFromString};");
    else
        mScriptEngine->evaluate("var jsondb = { emit: _jsondb.create, createUuidFromString: _jsondb.createUuidFromString };");
}

bool JsonDbMapDefinition::compileMapFunctions(QJSEngine *scriptEngine, QJsonObject definition, QMap<QString,QJSValue> &mapFunctions, QString &message)
{
    bool status = true;
    QJsonObject sourceFunctions(definition.contains("join")
                                ? definition.value("join").toObject()
                                : definition.value("map").toObject());
    for (QJsonObject::const_iterator it = sourceFunctions.begin(); it != sourceFunctions.end(); ++it) {
        const QString &sourceType = it.key();
        const QString &script = it.value().toString();
        QJSValue mapFunction =
            scriptEngine->evaluate(QString("var map_%1 = (%2); map_%1;").arg(QString(sourceType).replace(".", "_")).arg(script));
        if (mapFunction.isError() || !mapFunction.isCallable()) {
            message = QString( "Unable to parse map function: " + mapFunction.toString());
            status = false;
        }
        mapFunctions[sourceType] = mapFunction;
    }
    return status;
}

void JsonDbMapDefinition::releaseScriptEngine()
{
    mMapFunctions.clear();
    delete mScriptEngine;
    mScriptEngine = 0;
}


void JsonDbMapDefinition::initIndexes()
{
    mTargetTable->addIndexOnProperty(QLatin1String("_sourceUuids.*"), QLatin1String("string"), mTargetType);
}

void JsonDbMapDefinition::updateObject(const JsonDbObject &beforeObject, const JsonDbObject &afterObject)
{
    QHash<QString, JsonDbObject> unmappedObjects;
    mEmittedObjects.clear();

    if (!beforeObject.isEmpty()) {
        QJsonValue uuid = beforeObject.value(JsonDbString::kUuidStr);
        GetObjectsResult getObjectResponse = mTargetTable->getObjects("_sourceUuids.*", uuid, mTargetType);
        foreach (const JsonDbObject &unmappedObject, getObjectResponse.data) {
            QString uuid = unmappedObject.value(JsonDbString::kUuidStr).toString();
            unmappedObjects[uuid] = unmappedObject;
        }
    }

    mapObject(afterObject);

    for (QHash<QString, JsonDbObject>::const_iterator it = unmappedObjects.begin();
         it != unmappedObjects.end();
         ++it) {
        JsonDbObject unmappedObject = it.value();
        QString uuid = unmappedObject.value(JsonDbString::kUuidStr).toString();
        JsonDbWriteResult res;
        if (mEmittedObjects.contains(uuid)) {
            JsonDbObject emittedObject(mEmittedObjects.value(uuid));
            emittedObject.insert(JsonDbString::kVersionStr, unmappedObject.value(JsonDbString::kVersionStr));
            emittedObject.insert(JsonDbString::kOwnerStr, unmappedObject.value(JsonDbString::kOwnerStr));
            if (emittedObject == it.value()) {
                // skip duplicates
                continue;
            } else {
                // update changed view objects
                res = mPartition->updateObject(mOwner, emittedObject, JsonDbPartition::ViewObject);
            }

            mEmittedObjects.remove(uuid);
        } else {
            // remove unmatched objects
            unmappedObject.markDeleted();
            res = mPartition->updateObject(mOwner, unmappedObject, JsonDbPartition::ViewObject);
        }

        if (res.code != JsonDbError::NoError)
            setError("Error updating view object: " + res.message);
    }

    for (QHash<QString, JsonDbObject>::const_iterator it = mEmittedObjects.begin();
         it != mEmittedObjects.end();
         ++it) {
        JsonDbObject newItem(it.value());
        JsonDbWriteResult res = mPartition->updateObject(mOwner, newItem, JsonDbPartition::ViewObject);
        if (res.code != JsonDbError::NoError)
            setError("Error creating view object: " + res.message);
    }
}

QJSValue JsonDbMapDefinition::mapFunction(const QString &sourceType) const
{
    if (mMapFunctions.contains(sourceType))
        return mMapFunctions[sourceType];
    else
        return QJSValue();
}

void JsonDbMapDefinition::mapObject(JsonDbObject object)
{
    initScriptEngine();
    const QString &sourceType = object.value(JsonDbString::kTypeStr).toString();

    QJSValue sv = JsonDbObject::toJSValue(object, mScriptEngine);
    QString uuid = object.value(JsonDbString::kUuidStr).toString();
    mSourceUuids.clear();
    mSourceUuids.append(mUuid); // depends on the map definition object
    mSourceUuids.append(uuid);  // depends on the source object
    QJSValue mapped;

    QJSValueList mapArgs;
    mapArgs << sv;
    mapped = mapFunction(sourceType).call(mapArgs);

    if (mapped.isError())
        setError("Error executing map function: " + mapped.toString());
}

void JsonDbMapDefinition::unmapObject(const JsonDbObject &object)
{
    Q_ASSERT(object.value(JsonDbString::kUuidStr).type() == QJsonValue::String);
    initScriptEngine();
    QJsonValue uuid = object.value(JsonDbString::kUuidStr);
    GetObjectsResult getObjectResponse = mTargetTable->getObjects("_sourceUuids.*", uuid, mTargetType);
    JsonDbObjectList dependentObjects = getObjectResponse.data;

    for (int i = 0; i < dependentObjects.size(); i++) {
        JsonDbObject dependentObject = dependentObjects.at(i);
        if (dependentObject.value(JsonDbString::kTypeStr).toString() != mTargetType)
            continue;

        dependentObject.markDeleted();
        mPartition->updateObject(mOwner, dependentObject, JsonDbPartition::ViewObject);
    }
}

void JsonDbMapDefinition::lookupRequested(const QJSValue &query, const QJSValue &context)
{
    QString objectType = query.property("objectType").toString();
    // compatibility for old style maps
    if (mDefinition.value("map").isObject()) {
        if (objectType.isEmpty()) {
            setError("No objectType provided to jsondb.lookup");
            return;
        }
        if (!mSourceTypes.contains(objectType)) {
            setError(QString("lookup requested for type %1 not in source types: %2")
                     .arg(objectType)
                     .arg(mSourceTypes.join(", ")));
            return;
        }
    }
    QString findKey = query.property("index").toString();
    QJSValue findValue = query.property("value");
    GetObjectsResult getObjectResponse =
        mPartition->getObjects(findKey, JsonDbObject::fromJSValue(findValue), objectType, false);
    if (!getObjectResponse.error.isNull()) {
        if (jsondbSettings->verbose())
            qDebug() << "lookupRequested" << mSourceTypes << mTargetType
                     << getObjectResponse.error.toString();
        setError(getObjectResponse.error.toString());
    }
    JsonDbObjectList objectList = getObjectResponse.data;
    for (int i = 0; i < objectList.size(); ++i) {
        JsonDbObject object = objectList.at(i);
        const QString uuid = object.value(JsonDbString::kUuidStr).toString();
        if (mSourceUuids.contains(uuid)) {
            if (jsondbSettings->verbose())
                qDebug() << "Lookup cycle detected" << "key" << findKey << JsonDbObject::fromJSValue(findValue) << "matching object" << uuid << "source uuids" << mSourceUuids;
            continue;
        }
        mSourceUuids.append(uuid);
        QJSValueList mapArgs;
        QJSValue sv = JsonDbObject::toJSValue(object, mScriptEngine);

        mapArgs << sv << context;
        QJSValue mapped = mMapFunctions[objectType].call(mapArgs);

        if (mapped.isError())
            setError("Error executing map function during lookup: " + mapped.toString());

        mSourceUuids.removeLast();
    }
}

void JsonDbMapDefinition::viewObjectEmitted(const QJSValue &value)
{
    JsonDbObject newItem(JsonDbObject::fromJSValue(value).toObject());
    newItem.insert(JsonDbString::kTypeStr, mTargetType);
    QJsonArray sourceUuids;
    foreach (const QString &str, mSourceUuids)
        sourceUuids.append(str);
    newItem.insert("_sourceUuids", sourceUuids);

    if (!newItem.contains(JsonDbString::kUuidStr))
        newItem.generateUuid();

    QString uuid = newItem.value(JsonDbString::kUuidStr).toString();
    mEmittedObjects.insert(uuid, newItem);
}

bool JsonDbMapDefinition::isActive() const
{
    return !mDefinition.contains(JsonDbString::kActiveStr) || mDefinition.value(JsonDbString::kActiveStr).toBool();
}

void JsonDbMapDefinition::setError(const QString &errorMsg)
{
    mDefinition.insert(JsonDbString::kActiveStr, false);
    mDefinition.insert(JsonDbString::kErrorStr, errorMsg);
    if (mPartition)
        mPartition->updateObject(mOwner, mDefinition, JsonDbPartition::ForcedWrite);
}

bool JsonDbMapDefinition::validateDefinition(const JsonDbObject &map, JsonDbPartition *partition, QString &message)
{
    message.clear();
    QString targetType = map.value("targetType").toString();
    QString uuid = map.value(JsonDbString::kUuidStr).toString();
    JsonDbView *view = partition->findView(targetType);
    QStringList sourceTypes;

    if (targetType.isEmpty()) {
        message = QLatin1Literal("targetType property for Map not specified");
    } else if (map.contains(QLatin1Literal("sourceType"))) {
        message = QLatin1Literal("sourceType property for Map no longer supported");
    } else if (!view) {
        message = QLatin1Literal("targetType must be of a type that extends View");
    } else if (map.contains("join")) {
        QJsonObject sourceFunctions = map.value("join").toObject();
        sourceTypes = sourceFunctions.keys();
        if (sourceFunctions.isEmpty())
            message = QLatin1Literal("sourceTypes and functions for Map with join not specified");

        foreach (const QString &sourceType, sourceTypes) {
            if (sourceFunctions.value(sourceType).toString().isEmpty())
                message = QString("join function for source type '%1' not specified for Map").arg(sourceType);
            if (view->mMapDefinitionsBySource.contains(sourceType)
                && view->mMapDefinitionsBySource.value(sourceType)->uuid() != uuid)
                message =
                  QString("duplicate Map definition on source %1 and target %2")
                    .arg(sourceType).arg(targetType);
        }

        if (map.contains("map"))
            message = QLatin1Literal("Map 'join' and 'map' options are mutually exclusive");
    } else {
        QJsonValue mapValue = map.value("map");
        if (!mapValue.isObject())
            message = QLatin1String("sourceType property for Map not specified");
        else if (!mapValue.isString() && !mapValue.isObject())
            message = QLatin1String("map function for Map not specified");
    }
    // check for parse errors
    QJSEngine scriptEngine;
    QMap<QString,QJSValue> mapFunctions;
    compileMapFunctions(&scriptEngine, map, mapFunctions, message);

    return message.isEmpty();
}

QT_END_NAMESPACE_JSONDB
