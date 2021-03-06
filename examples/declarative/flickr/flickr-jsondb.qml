/****************************************************************************
**
** Copyright (C) 2012 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the QtDeclarative module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

import QtQuick 2.0
import QtQuick.Particles 2.0
import "content"
import QtJsonDb 1.0 as JsonDb

Item {
    id: screen; width: 320; height: 480
    property bool inGridView : true


    JsonDb.Partition {
        id: systemPartition
    }

    RssModel {
        id: rssModel
        function createCallback(error, response) {
            if (error) {
                console.log("Error Creating image "+error.code+" "+error.message);
                return;
            }
            for (var i = 0; i < response.items.length; i++) {
                console.log("Created Object : "+JSON.stringify(response.items[i]));
            }
        }

        onStatusChanged: {
            console.log("onStatusChange status=" + status);
            if (rssModel.status == 1) {
                for (var i = 0; i < rssModel.count; i++) {
                    var obj = rssModel.get(i);
                    obj['_type'] = "FlickrImage";
                    //console.log("i=" + i + " " + JSON.stringify(obj));
                    systemPartition.create(obj, createCallback);
                }
            }
        }
    }

    JsonDb.JsonDbSortingListModel {
        id: jsondblistmodel
        partitions: [systemPartition]
        query: "[?_type=\"FlickrImage\"]"
        sortOrder: "[/imageGuid]"
        roleNames: ["_uuid", "title", "imagePath", "url", "description", "tags", "photoWidth", "photoHeight", "photoType", "photoAuthor", "photoDate"]
    }

    VisualDataModel{
        id: vdm
        delegate: UnifiedDelegate{}
        model: jsondblistmodel
    }

    Rectangle {
        id: background
        anchors.fill: parent; color: "#343434";

        Image { source: "content/images/stripes.png"; fillMode: Image.Tile; anchors.fill: parent; opacity: 0.3 }
        ParticleSystem {
            id: bgParticles
            anchors.fill: parent
            ImageParticle {
                groups: ["trail"]
                source: "content/images/particle.png"
                color: "#1A1A6F"
                alpha: 0.1
                colorVariation: 0.01
                blueVariation: 0.8
            }
            Emitter {
                group: "drops"
                width: parent.width
                emitRate: 0.5
                lifeSpan: 20000
                startTime: 16000
                speed: PointDirection{
                    y: {screen.height/18}
                }
            }
            TrailEmitter {
                follow: "drops"
                group: "trail"
                emitRatePerParticle: 18
                size: 32
                endSize: 0
                sizeVariation: 4
                lifeSpan: 1200
                anchors.fill: parent
                emitWidth: 16
                emitHeight: 16
                emitShape: EllipseShape{}
            }
        }

        Item {
            id: views
            width: parent.width
            anchors.top: titleBar.bottom; anchors.bottom: toolBar.top

            GridView {
                id: photoGridView; model: vdm.parts.grid
                cacheBuffer: 1000
                cellWidth: (parent.width-2)/4; cellHeight: cellWidth; width: parent.width; height: parent.height
            }

            states: State {
                name: "GridView"; when: state.inGridView == true
            }

            transitions: Transition {
                NumberAnimation { properties: "x"; duration: 500; easing.type: Easing.InOutQuad }
            }

            ImageDetails { id: imageDetails; width: parent.width; anchors.left: views.right; height: parent.height }

            Item { id: foreground; anchors.fill: parent }
        }

        TitleBar { id: titleBar; width: parent.width; height: 40; opacity: 0.9 }

        ToolBar {
            id: toolBar
            height: 40; anchors.bottom: parent.bottom; width: parent.width; opacity: 0.9
            button1Label: "Update"; button2Label: "View mode"
            onButton1Clicked: rssModel.reload()
            onButton2Clicked: if (screen.inGridView == true) screen.inGridView = false; else screen.inGridView = true
        }

        Connections {
            target: imageDetails
            onClosed: {
                if (background.state == "DetailedView") {
                    background.state = '';
                    imageDetails.photoUrl = "";
                }
            }
        }

        states: State {
            name: "DetailedView"
            PropertyChanges { target: views; x: -parent.width }
            PropertyChanges { target: toolBar; button1Label: "View..." }
            PropertyChanges {
                target: toolBar
                onButton1Clicked: if (imageDetails.state=='') imageDetails.state='Back'; else imageDetails.state=''
            }
            PropertyChanges { target: toolBar; button2Label: "Back" }
            PropertyChanges { target: toolBar; onButton2Clicked: imageDetails.closed() }
        }

        transitions: Transition {
            NumberAnimation { properties: "x"; duration: 500; easing.type: Easing.InOutQuad }
        }

    }
}
