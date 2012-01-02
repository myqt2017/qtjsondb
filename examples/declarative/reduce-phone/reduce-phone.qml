/****************************************************************************
**
** Copyright (C) 2012 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** This file is part of the examples of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:BSD$
** You may use this file under the terms of the BSD license as follows:
**
** "Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
**   * Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**   * Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in
**     the documentation and/or other materials provided with the
**     distribution.
**   * Neither the name of Nokia Corporation and its Subsidiary(-ies) nor
**     the names of its contributors may be used to endorse or promote
**     products derived from this software without specific prior written
**     permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
** OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
** $QT_END_LICENSE$
**
****************************************************************************/

import QtQuick 2.0
import QtJsonDb 1.0 as JsonDb


Rectangle {
    width: 360
    height: 360

    property int fontsize: 16

    JsonDb.Partition {
        id: systemPartition
        name: "com.nokia.qtjsondb.System"
    }

    function createViewSchema()
    {
        var schema = {
            "_type": "_schemaType",
            "name": "ReducedPhoneView",
            "schema": {
                "extends": "View"
            }
        };
        systemPartition.find('[?_type="_schemaType"][?name="ReducedPhoneView"][/_type]',
             function (error, meta, response) {
                 if (error) {
                     console.log("Error " + response.status + " " + response.message);
                     return;
                 }
                 if (response.length > 0) {
                     console.log("Schema exists");
                     createReduce(false, {}, response);
                 } else {
                     console.log("Create Schema");
                     systemPartition.create(schema, createReduce);
                 }
            });
    }

    function createReduce(error, meta, response)
    {
        console.log("Creating reduce");
        if (error) {
            console.log("Error " + response.status + " " + response.message);
            return;
        }

        var reduceDefinition = {
            "_type": "Reduce",
            "sourceType": "PhoneView",
            "sourceKeyName": "phoneNumber",
            "targetType": "ReducedPhoneView",
            "add": (function (k, v0, v) {
                        if (!v0)
                            v0 = {
                                "phoneNumber": k,
                                "names": []
                            };
                        var name = v.firstName + " " + v.lastName;
                        if (v0.names.indexOf(name) < 0)
                            v0.names.push(name);
                        return v0;
                    }).toString(),
            "subtract": (function (k, v0, v) {
                         if (!v0)
                             v0 = {
                                 "phoneNumber": k,
                                 "names": []
                             };
                         var name = v.firstName + " " + v.lastName;
                         var pos = v0.names.indexOf(name);
                         if (pos >= 0)
                             v0.names.splice(pos, 1);
                         return v0;
                    }).toString()
        };
        systemPartition.find('[?_type="Reduce"][?targetType="ReducedPhoneView"]',
             function (error, meta, response) {
                 if (error) {
                     console.log("Error " + response.status + " " + response.message);
                     return;
                 }
                 if (response.length > 0) {
                     console.log("Reduce Object exists, set partition");
                     contacts.partition = systemPartition;
                 } else {
                     console.log("Create Reduce Object");
                     systemPartition.create(reduceDefinition, createReduce);
                 }
            });

    }


    JsonDb.JsonDbListModel {
        id: contacts
        query: '[?_type="ReducedPhoneView"][/key]'
        roleNames: ["key", "value"]
        limit: 100
    }

    Component.onCompleted: { createViewSchema();}
    ListView {
        id: listView
        anchors.top: parent.top
        anchors.bottom: statusText.top
        anchors.topMargin: 10
        anchors.bottomMargin: 10
        width: parent.width
        model: contacts
        highlight: Rectangle { color: "lightsteelblue"; radius: 5 ;width: 200;}
        focus: true
        delegate: Row {
            spacing: 10
            Text {
                text: key + ":   " + JSON.stringify(value.names)
                font.pointSize: fontsize
                MouseArea {
                   anchors.fill: parent;
                   onPressed: {
                       listView.currentIndex = index;
                   }
                }
            }
        }
    }
    Rectangle {
        id: statusText
        anchors.bottom: messageRectangle.top
        width: parent.width
        height: 20
        color:  "lightgray"
        Text {
            anchors.centerIn: parent
            font.pointSize: fontsize-4
            text: "Cache limit : " + contacts.limit + "  rowCount : " + contacts.rowCount + "  state : " + contacts.state
        }
    }
    Rectangle {
        id: messageRectangle
        anchors.bottom: parent.bottom
        width: parent.width
        height: 24
        color: "white"
        Text {
            id: messageText
            anchors.centerIn: parent
            font.pointSize: fontsize-4
            color: "red"
            text: ""
        }
    }
}