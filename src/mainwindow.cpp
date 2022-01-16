﻿#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "controlitem.h"

#include <QDateTime>
#ifdef Q_OS_ANDROID
#include <QBluetoothLocalDevice>
#include <QAndroidJniEnvironment>
#else
#include <QFileDialog>
#endif

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    contextMenu = new QMenu();
#ifdef Q_OS_ANDROID
    BTSocket = new QBluetoothSocket(QBluetoothServiceInfo::RfcommProtocol);
    IODevice = BTSocket;
    connect(BTSocket, &QBluetoothSocket::connected, this, &MainWindow::onBTConnectionChanged);
    connect(BTSocket, &QBluetoothSocket::disconnected, this, &MainWindow::onBTConnectionChanged);
    connect(BTSocket, QOverload<QBluetoothSocket::SocketError>::of(&QBluetoothSocket::error), this, &MainWindow::onBTConnectionChanged);
    BTdiscoveryAgent = new QBluetoothDeviceDiscoveryAgent(this);
    connect(BTdiscoveryAgent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this, &MainWindow::BTdeviceDiscovered);
    connect(BTdiscoveryAgent, &QBluetoothDeviceDiscoveryAgent::finished, this, &MainWindow::BTdiscoverFinished);
    connect(BTdiscoveryAgent, QOverload<QBluetoothDeviceDiscoveryAgent::Error>::of(&QBluetoothDeviceDiscoveryAgent::error), this, &MainWindow::BTdiscoverFinished);

    setStyleSheet("QCheckBox::indicator{min-width:15px;min-height:15px;}");

    // on Android, use default.
    settings = new QSettings("wh201906", "SerialTest");

#else
    serialPort = new QSerialPort();
    IODevice = serialPort;
    connect(serialPort, &QSerialPort::errorOccurred, this, &MainWindow::onSerialErrorOccurred);
    serialPortInfo = new QSerialPortInfo();

    baudRateLabel = new QLabel();
    dataBitsLabel = new QLabel();
    stopBitsLabel = new QLabel();
    parityLabel = new QLabel();
    onTopBox = new QCheckBox(tr("On Top"));
    connect(onTopBox, &QCheckBox::clicked, this, &MainWindow::onTopBoxClicked);

    // on PC, store preferences in files for portable use
    MySettings::init(QSettings::IniFormat, "preference.ini");
    settings = MySettings::defaultSettings();

    dockAllWindows = new QAction(tr("Dock all windows"), this);
    connect(dockAllWindows, &QAction::triggered, [ = ]()
    {
        for(int i = 0; i < dockList.size(); i++)
            dockList[i]->setFloating(false);
    });
    contextMenu->addAction(dockAllWindows);
    contextMenu->addSeparator();
#endif
    plotTab = new PlotTab();
    ui->funcTab->insertTab(0, plotTab, "PPlot");
    portLabel = new QLabel();
    stateButton = new QPushButton();
    TxLabel = new QLabel();
    RxLabel = new QLabel();
    IODeviceState = false;

    rawReceivedData = new QByteArray();
    rawSendedData = new QByteArray();
    RxUIBuf = new QByteArray();

    repeatTimer = new QTimer();
    updateUITimer = new QTimer();
    updateUITimer->setInterval(1);

    connect(ui->refreshPortsButton, &QPushButton::clicked, this, &MainWindow::refreshPortsInfo);
    connect(ui->sendEdit, &QLineEdit::returnPressed, this, &MainWindow::on_sendButton_clicked);

    connect(IODevice, &QIODevice::readyRead, this, &MainWindow::readData, Qt::QueuedConnection);

    connect(repeatTimer, &QTimer::timeout, this, &MainWindow::on_sendButton_clicked);
    connect(updateUITimer, &QTimer::timeout, this, &MainWindow::updateRxUI);
    connect(stateButton, &QPushButton::clicked, this, &MainWindow::onStateButtonClicked);

    RxSlider = ui->receivedEdit->verticalScrollBar();
    connect(RxSlider, &QScrollBar::valueChanged, this, &MainWindow::onRxSliderValueChanged);
    connect(RxSlider, &QScrollBar::sliderMoved, this, &MainWindow::onRxSliderMoved);

    refreshPortsInfo();
    initUI();
    loadPreference();

    connect(ui->receivedHexBox, &QCheckBox::clicked, this, &MainWindow::saveDataPreference);
    connect(ui->receivedLatestBox, &QCheckBox::clicked, this, &MainWindow::saveDataPreference);
    connect(ui->receivedRealtimeBox, &QCheckBox::clicked, this, &MainWindow::saveDataPreference);
    connect(ui->sendedHexBox, &QCheckBox::clicked, this, &MainWindow::saveDataPreference);
    connect(ui->data_suffixBox, &QGroupBox::clicked, this, &MainWindow::saveDataPreference);
    connect(ui->data_suffixTypeBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::saveDataPreference);
    connect(ui->data_suffixEdit, &QLineEdit::editingFinished, this, &MainWindow::saveDataPreference);
    connect(ui->data_repeatCheckBox, &QCheckBox::clicked, this, &MainWindow::saveDataPreference);
    connect(ui->repeatDelayEdit, &QLineEdit::editingFinished, this, &MainWindow::saveDataPreference);
    connect(ui->data_flowDTRBox, &QCheckBox::clicked, this, &MainWindow::saveDataPreference);
    connect(ui->data_flowRTSBox, &QCheckBox::clicked, this, &MainWindow::saveDataPreference);

    ui->ctrl_dataEdit->setVisible(false);

    myInfo = new QAction("wh201906", this);
    currVersion = new QAction(tr("Ver: ") + QApplication::applicationVersion().section('.', 0, -2), this); // ignore the 4th version number
    checkUpdate = new QAction(tr("Check Update"), this);
    connect(myInfo, &QAction::triggered, [ = ]()
    {
        QDesktopServices::openUrl(QUrl("https://github.com/wh201906"));
    });
    connect(checkUpdate, &QAction::triggered, [ = ]()
    {
        QDesktopServices::openUrl(QUrl("https://github.com/wh201906/SerialTest/releases"));
    });

    contextMenu->addAction(myInfo);
    currVersion->setEnabled(false);
    contextMenu->addAction(currVersion);
    contextMenu->addAction(checkUpdate);

    on_data_encodingSetButton_clicked();

}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::contextMenuEvent(QContextMenuEvent *event)
{
    contextMenu->exec(event->globalPos());
}

void MainWindow::onStateButtonClicked()
{
    QString portName;
#ifdef Q_OS_ANDROID
    portName = BTlastAddress;
#else
    portName = serialPort->portName();
#endif
    if(portName.isEmpty())
    {
        QMessageBox::warning(this, "Error", tr("Plz connect to a port first."));
        return;
    }
    if(IODeviceState)
    {
        IODevice->close();
        onIODeviceDisconnected();
    }
    else
    {
#ifdef Q_OS_ANDROID
        BTSocket->connectToService(QBluetoothAddress(BTlastAddress), QBluetoothUuid::SerialPort);
#else
        IODeviceState = IODevice->open(QIODevice::ReadWrite);
        if(IODeviceState)
            onIODeviceConnected();
        else
            QMessageBox::warning(this, "Error", tr("Cannot open the serial port."));
#endif
    }
}

void MainWindow::initUI()
{
    statusBar()->addWidget(portLabel, 1);
    statusBar()->addWidget(stateButton, 1);
    statusBar()->addWidget(RxLabel, 1);
    statusBar()->addWidget(TxLabel, 1);
#ifdef Q_OS_ANDROID
    ui->baudRateLabel->setVisible(false);
    ui->baudRateBox->setVisible(false);
    ui->advancedBox->setVisible(false);
    ui->portTable->hideColumn(HManufacturer);
    ui->portTable->hideColumn(HSerialNumber);
    ui->portTable->hideColumn(HIsNull);
    ui->portTable->hideColumn(HVendorID);
    ui->portTable->hideColumn(HProductID);
    ui->portTable->hideColumn(HBaudRates);

    ui->portTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->portTable->horizontalHeaderItem(HPortName)->setText(tr("DeviceName"));
    ui->portTable->horizontalHeaderItem(HDescription)->setText(tr("Type"));
    ui->portTable->horizontalHeaderItem(HSystemLocation)->setText(tr("MAC Address"));

    ui->portLabel->setText(tr("MAC Address") + ":");

    // keep screen on

    QAndroidJniObject helper("priv/wh201906/serialtest/BTHelper");
    QtAndroid::runOnAndroidThread([&]
    {
        helper.callMethod<void>("keepScreenOn", "(Landroid/app/Activity;)V", QtAndroid::androidActivity().object());
    });

    // Strange resize behavior on Android
    // Need a fixed size
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFixedSize(QApplication::primaryScreen()->availableGeometry().size());

    ui->data_flowControlBox->setVisible(false);

#else
    ui->flowControlBox->addItem(tr("NoFlowControl"));
    ui->flowControlBox->addItem(tr("HardwareControl"));
    ui->flowControlBox->addItem(tr("SoftwareControl"));
    ui->flowControlBox->setItemData(0, QSerialPort::NoFlowControl);
    ui->flowControlBox->setItemData(1, QSerialPort::HardwareControl);
    ui->flowControlBox->setItemData(2, QSerialPort::SoftwareControl);
    ui->parityBox->addItem(tr("NoParity"));
    ui->parityBox->addItem(tr("EvenParity"));
    ui->parityBox->addItem(tr("OddParity"));
    ui->parityBox->addItem(tr("SpaceParity"));
    ui->parityBox->addItem(tr("MarkParity"));
    ui->parityBox->setItemData(0, QSerialPort::NoParity);
    ui->parityBox->setItemData(1, QSerialPort::EvenParity);
    ui->parityBox->setItemData(2, QSerialPort::OddParity);
    ui->parityBox->setItemData(3, QSerialPort::SpaceParity);
    ui->parityBox->setItemData(4, QSerialPort::MarkParity);
    ui->stopBitsBox->addItem("1");
    ui->stopBitsBox->addItem("1.5");
    ui->stopBitsBox->addItem("2");
    ui->stopBitsBox->setItemData(0, QSerialPort::OneStop);
    ui->stopBitsBox->setItemData(1, QSerialPort::OneAndHalfStop);
    ui->stopBitsBox->setItemData(2, QSerialPort::TwoStop);
    ui->dataBitsBox->addItem("5");
    ui->dataBitsBox->addItem("6");
    ui->dataBitsBox->addItem("7");
    ui->dataBitsBox->addItem("8");
    ui->dataBitsBox->setItemData(0, QSerialPort::Data5);
    ui->dataBitsBox->setItemData(1, QSerialPort::Data6);
    ui->dataBitsBox->setItemData(2, QSerialPort::Data7);
    ui->dataBitsBox->setItemData(3, QSerialPort::Data8);
    ui->dataBitsBox->setCurrentIndex(3);

    statusBar()->addWidget(baudRateLabel, 1);
    statusBar()->addWidget(dataBitsLabel, 1);
    statusBar()->addWidget(stopBitsLabel, 1);
    statusBar()->addWidget(parityLabel, 1);
    statusBar()->addWidget(onTopBox, 1);
    dockInit();
#endif

    stateButton->setMinimumHeight(1);
    stateButton->setStyleSheet("*{text-align:left;}");

    on_advancedBox_clicked(false);
    stateUpdate();
}

void MainWindow::refreshPortsInfo()
{
    ui->portTable->clearContents();
    ui->portBox->clear();
#ifdef Q_OS_ANDROID
    ui->refreshPortsButton->setText(tr("Searching..."));
    QAndroidJniEnvironment env;
    QtAndroid::PermissionResult r = QtAndroid::checkPermission("android.permission.ACCESS_FINE_LOCATION");
    if(r == QtAndroid::PermissionResult::Denied)
    {
        QtAndroid::requestPermissionsSync(QStringList() << "android.permission.ACCESS_FINE_LOCATION");
        r = QtAndroid::checkPermission("android.permission.ACCESS_FINE_LOCATION");
        if(r == QtAndroid::PermissionResult::Denied)
        {
            qDebug() << "failed to request";
        }
    }
    qDebug() << "has permission";

    QAndroidJniObject helper("priv/wh201906/serialtest/BTHelper");
    qDebug() << "test:" << helper.callObjectMethod<jstring>("TestStr").toString();
    QAndroidJniObject array = helper.callObjectMethod("getBondedDevices", "()[Ljava/lang/String;");
    int arraylen = env->GetArrayLength(array.object<jarray>());
    qDebug() << "arraylen:" << arraylen;
    ui->portTable->setRowCount(arraylen);
    for(int i = 0; i < arraylen; i++)
    {
        QString info = QAndroidJniObject::fromLocalRef(env->GetObjectArrayElement(array.object<jobjectArray>(), i)).toString();
        QString address = info.left(info.indexOf(' '));
        QString name = info.right(info.length() - info.indexOf(' ') - 1);
        qDebug() << address << name;
        ui->portTable->setItem(i, HPortName, new QTableWidgetItem(name));
        ui->portTable->setItem(i, HSystemLocation, new QTableWidgetItem(address));
        ui->portTable->setItem(i, HDescription, new QTableWidgetItem(tr("Bonded")));
        ui->portBox->addItem(address);
    }

    BTdiscoveryAgent->start();
#else
    QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    ui->portTable->setRowCount(ports.size());
    for(int i = 0; i < ports.size(); i++)
    {
        ui->portTable->setItem(i, HPortName, new QTableWidgetItem(ports[i].portName()));
        ui->portBox->addItem(ports[i].portName());
        ui->portTable->setItem(i, HDescription, new QTableWidgetItem(ports[i].description()));
        ui->portTable->setItem(i, HManufacturer, new QTableWidgetItem(ports[i].manufacturer()));
        ui->portTable->setItem(i, HSerialNumber, new QTableWidgetItem(ports[i].serialNumber()));
        ui->portTable->setItem(i, HIsNull, new QTableWidgetItem(ports[i].isNull() ? "Yes" : "No"));
        ui->portTable->setItem(i, HSystemLocation, new QTableWidgetItem(ports[i].systemLocation()));
        ui->portTable->setItem(i, HVendorID, new QTableWidgetItem(QString::number(ports[i].vendorIdentifier())));
        ui->portTable->setItem(i, HProductID, new QTableWidgetItem(QString::number(ports[i].productIdentifier())));

        QList<qint32> baudRateList = ports[i].standardBaudRates();
        QString baudRates = "";
        for(int j = 0; j < baudRates.size(); j++)
        {
            baudRates += QString::number(baudRateList[j]) + ", ";
        }
        ui->portTable->setItem(i, HBaudRates, new QTableWidgetItem(baudRates));
    }
#endif
}

void MainWindow::on_portTable_cellClicked(int row, int column)
{
    Q_UNUSED(column);
    ui->portBox->setCurrentIndex(row);
#ifndef Q_OS_ANDROID
    QStringList preferences = settings->childGroups();
    QStringList::iterator it;


    // search preference by <vendorID>-<productID>
    QString id = ui->portTable->item(row, HVendorID)->text();  // vendor id
    id += "-";
    id += ui->portTable->item(row, HProductID)->text(); // product id
    for(it = preferences.begin(); it != preferences.end(); it++)
    {
        if(*it == id)
        {
            loadPortPreference(id);
            break;
        }
    }
    if(it != preferences.end())
        return;

    // search preference by PortName
    id = ui->portTable->item(row, HPortName)->text();
    for(it = preferences.begin(); it != preferences.end(); it++)
    {
        if(*it == id)
        {
            loadPortPreference(id);
            break;
        }
    }
#endif
}

void MainWindow::on_advancedBox_clicked(bool checked)
{
    ui->dataBitsLabel->setVisible(checked);
    ui->dataBitsBox->setVisible(checked);
    ui->stopBitsLabel->setVisible(checked);
    ui->stopBitsBox->setVisible(checked);
    ui->parityLabel->setVisible(checked);
    ui->parityBox->setVisible(checked);
    ui->flowControlLabel->setVisible(checked);
    ui->flowControlBox->setVisible(checked);
}

void MainWindow::on_openButton_clicked()
{
#ifdef Q_OS_ANDROID
    BTSocket->connectToService(QBluetoothAddress(ui->portBox->currentText()), QBluetoothUuid::SerialPort);
#else
    serialPort->setPortName(ui->portBox->currentText());
    serialPort->setBaudRate(ui->baudRateBox->currentText().toInt());
    serialPort->setDataBits((QSerialPort::DataBits)ui->dataBitsBox->currentData().toInt());
    serialPort->setStopBits((QSerialPort::StopBits)ui->stopBitsBox->currentData().toInt());
    serialPort->setParity((QSerialPort::Parity)ui->parityBox->currentData().toInt());
    serialPort->setFlowControl((QSerialPort::FlowControl)ui->flowControlBox->currentData().toInt());
    if(serialPort->isOpen())
    {
        QMessageBox::warning(this, "Error", "The port has been opened.");
        return;
    }
    if(!serialPort->open(QSerialPort::ReadWrite))
    {
        QMessageBox::warning(this, "Error", tr("Cannot open the serial port."));
        return;
    }
    onIODeviceConnected();
    savePortPreference(serialPort->portName());
#endif
}

void MainWindow::on_closeButton_clicked()
{
    IODevice->close();
    onIODeviceDisconnected();
}

void MainWindow::stateUpdate()
{

    QString portName;
#ifdef Q_OS_ANDROID
    portName = BTSocket->peerName();
#else
    portName = serialPort->portName();
    QString stopbits[4] = {"", tr("OneStop"), tr("TwoStop"), tr("OneAndHalfStop")};
    QString parities[6] = {tr("NoParity"), "", tr("EvenParity"), tr("OddParity"), tr("SpaceParity"), tr("MarkParity")};
    if(IODeviceState)
    {
        baudRateLabel->setText(tr("BaudRate") + ": " + QString::number(serialPort->baudRate()));
        dataBitsLabel->setText(tr("DataBits") + ": " + QString::number(serialPort->dataBits()));
        stopBitsLabel->setText(tr("StopBits") + ": " + stopbits[(int)serialPort->stopBits()]);
        parityLabel->setText(tr("Parity") + ": " + parities[(int)serialPort->parity()]);
    }
    else
    {
        baudRateLabel->setText(tr("BaudRate") + ": ");
        dataBitsLabel->setText(tr("DataBits") + ": ");
        stopBitsLabel->setText(tr("StopBits") + ": ");
        parityLabel->setText(tr("Parity") + ": ");
    }
#endif
    if(IODeviceState)
        stateButton->setText(tr("State") + ": √");
    else
        stateButton->setText(tr("State") + ": X");
    portLabel->setText(tr("Port") + ": " + portName);
    RxLabel->setText(tr("Rx") + ": " + QString::number(rawReceivedData->length()));
    TxLabel->setText(tr("Tx") + ": " +  QString::number(rawSendedData->length()));
}

void MainWindow::onIODeviceConnected()
{


    qDebug() << "IODevice Connected";
    IODeviceState = true;
    updateUITimer->start();
    stateUpdate();
    refreshPortsInfo();
#ifndef Q_OS_ANDROID
    QSerialPort* port;
    port = dynamic_cast<QSerialPort*>(IODevice);
    if(port != nullptr)
    {
        ui->data_flowRTSBox->setVisible(port->flowControl() != QSerialPort::HardwareControl);
        ui->data_flowRTSBox->setChecked(port->isRequestToSend());
        ui->data_flowDTRBox->setChecked(port->isDataTerminalReady());
    }
#endif
}

void MainWindow::onIODeviceDisconnected()
{
    qDebug() << "IODevice Disconnected";
    IODeviceState = false;
    updateUITimer->stop();
    stateUpdate();
    refreshPortsInfo();
    updateRxUI();
}

// Rx/Tx Data
// **********************************************************************************************************************************************

void MainWindow::syncReceivedEditWithData()
{
    RxSlider->blockSignals(true);
    if(isReceivedDataHex)
        ui->receivedEdit->setPlainText(rawReceivedData->toHex(' ') + ' ');
    else
        // sync, use QTextCodec
        ui->receivedEdit->setPlainText(dataCodec->toUnicode(*rawReceivedData));
    RxSlider->blockSignals(false);
//    qDebug() << toHEX(*rawReceivedData);
}

void MainWindow::syncSendedEditWithData()
{
    if(isSendedDataHex)
        ui->sendedEdit->setPlainText(rawSendedData->toHex(' ') + ' ');
    else
        ui->sendedEdit->setPlainText(dataCodec->toUnicode(*rawSendedData));
}

// TODO:
// split sync process, add processEvents()
// void MainWindow::syncEditWithData()

void MainWindow::appendReceivedData(const QByteArray& data)
{
    int cursorPos;
    int sliderPos;
    sliderPos = RxSlider->sliderPosition();

    cursorPos = ui->receivedEdit->textCursor().position();
    ui->receivedEdit->moveCursor(QTextCursor::End);
    if(isReceivedDataHex)
    {
        ui->receivedEdit->insertPlainText(data.toHex(' ') + ' ');
        hexCounter += data.length();
        // QPlainTextEdit is not good at handling long line
        // Seperate for better realtime receiving response
        if(hexCounter > 5000)
        {
            ui->receivedEdit->insertPlainText("\r\n");
            hexCounter = 0;
        }
    }
    else
    {
        // append, use QTextDecoder
        // if \r and \n are received seperatedly, the rawReceivedData will be fine, but the receivedEdit will have one more empty line
        // just ignore one of them
        if(lastReceivedByte == '\r' && !data.isEmpty() && *data.cbegin() == '\n')
            ui->receivedEdit->insertPlainText(RxDecoder->toUnicode(data.right(data.size() - 1)));
        else
            ui->receivedEdit->insertPlainText(RxDecoder->toUnicode(data));
        lastReceivedByte = *data.crbegin();
    }
    ui->receivedEdit->textCursor().setPosition(cursorPos);
    RxSlider->setSliderPosition(sliderPos);
}

void MainWindow::readData()
{
    QByteArray newData = IODevice->readAll();
    if(newData.isEmpty())
        return;
    rawReceivedData->append(newData);
    if(ui->receivedLatestBox->isChecked())
    {
        userRequiredRxSliderPos = RxSlider->maximum();
        RxSlider->setSliderPosition(RxSlider->maximum());
    }
    else
    {
        userRequiredRxSliderPos = currRxSliderPos;
        RxSlider->setSliderPosition(currRxSliderPos);
    }
    RxLabel->setText(tr("Rx") + ": " + QString::number(rawReceivedData->length()));
    RxUIBuf->append(newData);
    QApplication::processEvents();
}

void MainWindow::on_sendButton_clicked()
{
    QByteArray data;
    if(isSendedDataHex)
        data = QByteArray::fromHex(ui->sendEdit->text().toLatin1());
    else
        data = dataCodec->fromUnicode(ui->sendEdit->text());
    if(ui->data_suffixBox->isChecked())
    {
        if(ui->data_suffixTypeBox->currentIndex() == 0)
            data += dataCodec->fromUnicode(ui->data_suffixEdit->text());
        else if(ui->data_suffixTypeBox->currentIndex() == 1)
            data += QByteArray::fromHex(ui->data_suffixEdit->text().toLatin1());
        else if(ui->data_suffixTypeBox->currentIndex() == 2)
            data += "\r\n";
        else if(ui->data_suffixTypeBox->currentIndex() == 3)
            data += "\n";
    }

    sendData(data);
}

void MainWindow::sendData(QByteArray& data)
{
    if(!IODeviceState)
    {
        QMessageBox::warning(this, tr("Error"), tr("No port is opened."));
        ui->data_repeatCheckBox->setCheckState(Qt::Unchecked);
        return;
    }
    rawSendedData->append(data);
    syncSendedEditWithData();
    IODevice->write(data);
    TxLabel->setText("Tx: " + QString::number(rawSendedData->length()));
}

// Rx/Tx UI
// **********************************************************************************************************************************************

void MainWindow::onRxSliderValueChanged(int value)
{
    // qDebug() << "valueChanged" << value;
    currRxSliderPos = value;
}

void MainWindow::onRxSliderMoved(int value)
{
    // slider is moved by user
    // qDebug() << "sliderMoved" << value;
    userRequiredRxSliderPos = value;
}

void MainWindow::on_sendedHexBox_stateChanged(int arg1)
{
    isSendedDataHex = (arg1 == Qt::Checked);
    syncSendedEditWithData();
}

void MainWindow::on_receivedHexBox_stateChanged(int arg1)
{
    isReceivedDataHex = (arg1 == Qt::Checked);
    syncReceivedEditWithData();
}

void MainWindow::on_receivedClearButton_clicked()
{
    lastReceivedByte = '\0'; // anything but '\r'
    rawReceivedData->clear();
    RxLabel->setText(tr("Rx") + ": " + QString::number(rawReceivedData->length()));
    syncReceivedEditWithData();
}

void MainWindow::on_sendedClearButton_clicked()
{
    rawSendedData->clear();
    TxLabel->setText(tr("Tx") + ": " + QString::number(rawSendedData->length()));
    syncSendedEditWithData();
}

void MainWindow::on_sendEdit_textChanged(const QString &arg1)
{
    Q_UNUSED(arg1);
    repeatTimer->stop();
    ui->data_repeatBox->setChecked(false);
}

void MainWindow::on_data_repeatCheckBox_stateChanged(int arg1)
{
    if(arg1 == Qt::Checked)
    {
        repeatTimer->setInterval(ui->repeatDelayEdit->text().toInt());
        repeatTimer->start();
    }
    else
        repeatTimer->stop();
}

void MainWindow::on_receivedCopyButton_clicked()
{
    QApplication::clipboard()->setText(ui->receivedEdit->toPlainText());
}

void MainWindow::on_sendedCopyButton_clicked()
{
    QApplication::clipboard()->setText(ui->sendedEdit->toPlainText());
}

void MainWindow::on_receivedExportButton_clicked()
{
    bool flag = true;
    QString fileName, selection;
    fileName = QFileDialog::getSaveFileName(this, tr("Export received data"), QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss") + ".txt");
    if(fileName.isEmpty())
        return;
    QFile file(fileName);
    selection = ui->receivedEdit->textCursor().selectedText().replace(QChar(0x2029), '\n');
    if(selection.isEmpty())
    {
        flag &= file.open(QFile::WriteOnly);
        flag &= file.write(*rawReceivedData) != -1;
    }
    else
    {
        flag &= file.open(QFile::WriteOnly | QFile::Text);
        flag &= file.write(selection.replace(QChar(0x2029), '\n').toUtf8()) != -1;
    }
    file.close();
    QMessageBox::information(this, tr("Info"), flag ? tr("Successed!") : tr("Failed!"));
}

void MainWindow::on_sendedExportButton_clicked()
{
    bool flag = true;
    QString fileName, selection;
    fileName = QFileDialog::getSaveFileName(this, tr("Export sended data"), QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss") + ".txt");
    if(fileName.isEmpty())
        return;
    QFile file(fileName);
    selection = ui->sendedEdit->textCursor().selectedText().replace(QChar(0x2029), '\n');
    if(selection.isEmpty())
    {
        flag &= file.open(QFile::WriteOnly);
        flag &= file.write(*rawSendedData) != -1;
    }
    else
    {
        flag &= file.open(QFile::WriteOnly | QFile::Text);
        flag &= file.write(selection.replace(QChar(0x2029), '\n').toUtf8()) != -1;
    }
    file.close();
    QMessageBox::information(this, tr("Info"), flag ? tr("Successed!") : tr("Failed!"));
}

void MainWindow::on_receivedUpdateButton_clicked()
{
    syncReceivedEditWithData();
}

// TODO:
// use the same RxDecoder for edit/plot
// maybe standalone decoder?
void MainWindow::updateRxUI()
{
    if(RxUIBuf->isEmpty())
        return;
    if(ui->receivedRealtimeBox->isChecked())
        appendReceivedData(*RxUIBuf);
    plotTab->newData(*RxUIBuf);
    RxUIBuf->clear();
}

// platform specific
// **********************************************************************************************************************************************

#ifdef Q_OS_ANDROID
void MainWindow::BTdiscoverFinished()
{
    ui->refreshPortsButton->setText(tr("Refresh"));
}

void MainWindow::BTdeviceDiscovered(const QBluetoothDeviceInfo &device)
{
    QString address = device.address().toString();
    QString name = device.name();
    int i = ui->portTable->rowCount();
    ui->portTable->setRowCount(i + 1);
    ui->portTable->setItem(i, HPortName, new QTableWidgetItem(name));
    ui->portTable->setItem(i, HSystemLocation, new QTableWidgetItem(address));
    ui->portTable->setItem(i, HDescription, new QTableWidgetItem(tr("Discovered")));
    ui->portBox->addItem(address);
    qDebug() << name
             << address
             << device.isValid()
             << device.rssi()
             << device.majorDeviceClass()
             << device.minorDeviceClass()
             << device.serviceClasses()
             << device.manufacturerData();
}

void MainWindow::onBTConnectionChanged()
{
    if(BTSocket->isOpen())
    {
        onIODeviceConnected();
        BTlastAddress = ui->portBox->currentText();
    }
    else
        onIODeviceDisconnected();
}
#else

void MainWindow::dockInit()
{
    setDockNestingEnabled(true);
    QDockWidget* dock;
    QWidget* widget;
    int count = ui->funcTab->count();
    for(int i = 0; i < count; i++)
    {
        dock = new QDockWidget(ui->funcTab->tabText(0), this);
        qDebug() << "dock name" << ui->funcTab->tabText(0);
        dock->setFeatures(QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetMovable);// movable is necessary, otherwise the dock cannot be dragged
        dock->setAllowedAreas(Qt::AllDockWidgetAreas);
        dock->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        widget = ui->funcTab->widget(0);
        dock->setWidget(widget);
        addDockWidget(Qt::BottomDockWidgetArea, dock);
        if(!dockList.isEmpty())
            tabifyDockWidget(dockList[0], dock);
        dockList.append(dock);
    }
    ui->funcTab->setVisible(false);
    ui->centralwidget->setVisible(false);
    dockList[0]->setVisible(true);
    dockList[0]->raise();
}

void MainWindow::onTopBoxClicked(bool checked)
{
    setWindowFlag(Qt::WindowStaysOnTopHint, checked);
    show();
}

void MainWindow::onSerialErrorOccurred(QSerialPort::SerialPortError error)
{
    qDebug() << error;
    if(error != QSerialPort::NoError && IODeviceState)
    {
        IODevice->close();
        onIODeviceDisconnected();
    }

}

void MainWindow::savePortPreference(const QString& portName)
{
    QSerialPortInfo info(portName);
    QString id;
    if(info.vendorIdentifier() != 0 && info.productIdentifier() != 0)
        id = QString::number(info.vendorIdentifier()) + "-" + QString::number(info.productIdentifier());
    else
        id = portName;
    settings->beginGroup(id);
    settings->setValue("BaudRate", ui->baudRateBox->currentText());
    settings->setValue("DataBitsID", ui->dataBitsBox->currentIndex());
    settings->setValue("StopBitsID", ui->stopBitsBox->currentIndex());
    settings->setValue("ParityID", ui->parityBox->currentIndex());
    settings->setValue("FlowControlID", ui->flowControlBox->currentIndex());
    settings->endGroup();
}

void MainWindow::loadPortPreference(const QString& id)
{
    settings->beginGroup(id);
    ui->baudRateBox->setEditText(settings->value("BaudRate").toString());
    ui->dataBitsBox->setCurrentIndex(settings->value("DataBitsID").toInt());
    ui->stopBitsBox->setCurrentIndex(settings->value("StopBitsID").toInt());
    ui->parityBox->setCurrentIndex(settings->value("ParityID").toInt());
    ui->flowControlBox->setCurrentIndex(settings->value("FlowControlID").toInt());
    settings->endGroup();
}

void MainWindow::on_data_flowDTRBox_clicked(bool checked)
{
    QSerialPort* port = dynamic_cast<QSerialPort*>(IODevice);
    if(port != nullptr)
        port->setDataTerminalReady(checked);
}

void MainWindow::on_data_flowRTSBox_clicked(bool checked)
{
    QSerialPort* port = dynamic_cast<QSerialPort*>(IODevice);
    if(port != nullptr && port->flowControl() != QSerialPort::HardwareControl)
        port->setRequestToSend(checked);
}
#endif

void MainWindow::saveDataPreference()
{
    settings->beginGroup("SerialTest_Data");
    settings->setValue("Recv_Hex", ui->receivedHexBox->isChecked());
    settings->setValue("Recv_Latest", ui->receivedLatestBox->isChecked());
    settings->setValue("Recv_Realtime", ui->receivedRealtimeBox->isChecked());
    settings->setValue("Send_Hex", ui->sendedHexBox->isChecked());
    settings->setValue("Suffix_Enabled", ui->data_suffixBox->isChecked());
    settings->setValue("Suffix_Type", ui->data_suffixTypeBox->currentIndex());
    settings->setValue("Suffix_Context", ui->data_suffixEdit->text());
    settings->setValue("Repeat_Enabled", ui->data_repeatCheckBox->isChecked());
    settings->setValue("Repeat_Delay", ui->repeatDelayEdit->text());
    settings->setValue("Flow_DTR", ui->data_flowDTRBox->isChecked());
    settings->setValue("Flow_RTS", ui->data_flowRTSBox->isChecked());
    //Encoding_Name will not be saved there, because it need to be verified
    settings->endGroup();

}

// settings->setValue\((.+), ui->(.+)->currentIndex.+
// ui->$2->setCurrentIndex(settings->value($1).toInt());
// settings->setValue\((.+), ui->(.+)->text.+
// ui->$2->setText(settings->value($1).toString());

void MainWindow::loadPreference()
{
    // default preferences are defined there
    settings->beginGroup("SerialTest_Data");
    ui->receivedHexBox->setChecked(settings->value("Recv_Hex", false).toBool());
    ui->receivedLatestBox->setChecked(settings->value("Recv_Latest", false).toBool());
    ui->receivedRealtimeBox->setChecked(settings->value("Recv_Realtime", true).toBool());
    ui->sendedHexBox->setChecked(settings->value("Send_Hex", false).toBool());
    ui->data_suffixBox->setChecked(settings->value("Suffix_Enabled", false).toBool());
    ui->data_suffixTypeBox->setCurrentIndex(settings->value("Suffix_Type", 2).toInt());
    ui->data_suffixEdit->setText(settings->value("Suffix_Context", "").toString());
    on_data_suffixTypeBox_currentIndexChanged(ui->data_suffixTypeBox->currentIndex());
    ui->data_repeatCheckBox->setChecked(settings->value("Repeat_Enabled", false).toBool());
    ui->repeatDelayEdit->setText(settings->value("Repeat_Delay", 1000).toString());
    ui->data_flowDTRBox->setChecked(settings->value("Flow_DTR", false).toBool());
    ui->data_flowRTSBox->setChecked(settings->value("Flow_RTS", false).toBool());
    ui->data_encodingNameBox->setCurrentText(settings->value("Encoding_Name", "UTF-8").toString());
    settings->endGroup();
}


void MainWindow::on_ctrl_addCMDButton_clicked()
{
    QBoxLayout* p = static_cast<QBoxLayout*>(ui->ctrl_itemContents->layout());
    ControlItem* c = new ControlItem(ControlItem::Command);
    connect(c, &ControlItem::send, this, &MainWindow::sendData);
    connect(c, &ControlItem::destroyed, this, &MainWindow::onCtrlItemDestroyed);
    c->setCodecPtr(dataCodec);
    p->insertWidget(ctrlItemCount++, c);
}


void MainWindow::on_ctrl_addSliderButton_clicked()
{
    QBoxLayout* p = static_cast<QBoxLayout*>(ui->ctrl_itemContents->layout());
    ControlItem* c = new ControlItem(ControlItem::Slider);
    connect(c, &ControlItem::send, this, &MainWindow::sendData);
    connect(c, &ControlItem::destroyed, this, &MainWindow::onCtrlItemDestroyed);
    p->insertWidget(ctrlItemCount++, c);
}


void MainWindow::on_ctrl_addCheckBoxButton_clicked()
{
    QBoxLayout* p = static_cast<QBoxLayout*>(ui->ctrl_itemContents->layout());
    ControlItem* c = new ControlItem(ControlItem::CheckBox);
    connect(c, &ControlItem::send, this, &MainWindow::sendData);
    connect(c, &ControlItem::destroyed, this, &MainWindow::onCtrlItemDestroyed);
    p->insertWidget(ctrlItemCount++, c);
}


void MainWindow::on_ctrl_addSpinBoxButton_clicked()
{
    QBoxLayout* p = static_cast<QBoxLayout*>(ui->ctrl_itemContents->layout());
    ControlItem* c = new ControlItem(ControlItem::SpinBox);
    connect(c, &ControlItem::send, this, &MainWindow::sendData);
    connect(c, &ControlItem::destroyed, this, &MainWindow::onCtrlItemDestroyed);
    p->insertWidget(ctrlItemCount++, c);
}

void MainWindow::onCtrlItemDestroyed()
{
    ctrlItemCount--;
}


void MainWindow::on_ctrl_clearButton_clicked()
{
    const QList<ControlItem*> list = ui->ctrl_itemContents->findChildren<ControlItem*>(QString(), Qt::FindDirectChildrenOnly);
    for(auto it = list.begin(); it != list.end(); it++)
        (*it)->deleteLater();
}

void MainWindow::on_data_suffixTypeBox_currentIndexChanged(int index)
{
    ui->data_suffixEdit->setVisible(index != 2 && index != 3);
    ui->data_suffixEdit->setPlaceholderText(tr("Suffix") + ((index == 1) ? "(Hex)" : ""));
}


void MainWindow::on_ctrl_importButton_clicked()
{
#ifdef Q_OS_ANDROID
    if(ui->ctrl_importButton->text() == tr("Import"))
    {
        ui->ctrl_dataEdit->setPlainText("# " + tr("Paste the exported data in the box."));
        ui->ctrl_dataEdit->appendPlainText(""); // new line;
        ui->ctrl_itemArea->setVisible(false);
        ui->ctrl_dataEdit->setVisible(true);
        ui->ctrl_importButton->setText(tr("Done"));
    }
    else
    {
        QBoxLayout* p = static_cast<QBoxLayout*>(ui->ctrl_itemContents->layout());
        QStringList dataList = ui->ctrl_dataEdit->toPlainText().split("\n", Qt::SkipEmptyParts);
        for(auto it = dataList.begin(); it != dataList.end(); it++)
        {
            if(it->at(0) == '#')
                continue;
            ControlItem* c = new ControlItem(ControlItem::Command);
            connect(c, &ControlItem::send, this, &MainWindow::sendData);
            connect(c, &ControlItem::destroyed, this, &MainWindow::onCtrlItemDestroyed);
            p->insertWidget(ctrlItemCount++, c);
            if(!c->load(*it))
                c->deleteLater();
        }
        ui->ctrl_dataEdit->clear();
        ui->ctrl_itemArea->setVisible(true);
        ui->ctrl_dataEdit->setVisible(false);
        ui->ctrl_importButton->setText(tr("Import"));
    }
#else
    bool flag = true;
    const QList<ControlItem*> list = ui->ctrl_itemContents->findChildren<ControlItem*>(QString(), Qt::FindDirectChildrenOnly);
    QString fileName;
    QBoxLayout* p = static_cast<QBoxLayout*>(ui->ctrl_itemContents->layout());
    fileName = QFileDialog::getOpenFileName(this, tr("Import Control Panel"));
    if(fileName.isEmpty())
        return;
    QFile file(fileName);
    flag &= file.open(QFile::ReadOnly | QFile::Text);
    QStringList dataList = QString(file.readAll()).split("\n", Qt::SkipEmptyParts);
    for(auto it = dataList.begin(); it != dataList.end(); it++)
    {
        if(it->at(0) == '#')
            continue;
        ControlItem* c = new ControlItem(ControlItem::Command);
        connect(c, &ControlItem::send, this, &MainWindow::sendData);
        connect(c, &ControlItem::destroyed, this, &MainWindow::onCtrlItemDestroyed);
        p->insertWidget(ctrlItemCount++, c);
        if(!c->load(*it))
            c->deleteLater();
    }
    file.close();
    QMessageBox::information(this, tr("Info"), flag ? tr("Successed!") : tr("Failed!"));
#endif
}


void MainWindow::on_ctrl_exportButton_clicked()
{
#ifdef Q_OS_ANDROID
    if(ui->ctrl_exportButton->text() == tr("Export"))
    {
        if(ctrlItemCount == 0)
        {
            QMessageBox::information(this, tr("Info"), tr("Please add item first"));
            return;
        }
        const QList<ControlItem*> list = ui->ctrl_itemContents->findChildren<ControlItem*>(QString(), Qt::FindDirectChildrenOnly);
        ui->ctrl_dataEdit->setPlainText("# " + tr("Copy all text in this box and save it to somewhere."));
        ui->ctrl_dataEdit->appendPlainText("# " + tr("To import, click the Import button, then paste the text back."));
        for(auto it = list.begin(); it != list.end(); it++)
            ui->ctrl_dataEdit->appendPlainText((*it)->save());
        ui->ctrl_itemArea->setVisible(false);
        ui->ctrl_dataEdit->setVisible(true);
        ui->ctrl_exportButton->setText(tr("Done"));
    }
    else
    {
        ui->ctrl_dataEdit->clear();
        ui->ctrl_itemArea->setVisible(true);
        ui->ctrl_dataEdit->setVisible(false);
        ui->ctrl_exportButton->setText(tr("Export"));
    }
#else
    if(ctrlItemCount == 0)
    {
        QMessageBox::information(this, tr("Info"), tr("Please add item first"));
        return;
    }
    bool flag = true;
    const QList<ControlItem*> list = ui->ctrl_itemContents->findChildren<ControlItem*>(QString(), Qt::FindDirectChildrenOnly);
    QString fileName;
    fileName = QFileDialog::getSaveFileName(this, tr("Export Control Panel"), QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss") + ".txt");
    if(fileName.isEmpty())
        return;
    QFile file(fileName);
    flag &= file.open(QFile::WriteOnly | QFile::Text);
    for(auto it = list.begin(); it != list.end(); it++)
        flag &= file.write(((*it)->save() + "\n").toUtf8()) != -1;
    file.close();
    QMessageBox::information(this, tr("Info"), flag ? tr("Successed!") : tr("Failed!"));
#endif
}

void MainWindow::on_data_encodingSetButton_clicked()
{
    QTextCodec* newCodec;
    QComboBox* box;
    box = ui->data_encodingNameBox;
    newCodec = QTextCodec::codecForName(box->currentText().toLatin1());
    if(newCodec != nullptr)
    {
        if(box->itemText(box->currentIndex()) == box->currentText()) // existing text
            dataEncodingId = box->currentIndex();
        if(RxDecoder != nullptr)
            delete RxDecoder;
        dataCodec = newCodec;
        RxDecoder = dataCodec->makeDecoder(); // clear state machine
        settings->beginGroup("SerialTest_Data");
        settings->setValue("Encoding_Name", ui->data_encodingNameBox->currentText());
        settings->endGroup();
    }
    else
    {
        QMessageBox::information(this, tr("Info"), ui->data_encodingNameBox->currentText() + " " + tr("is not a valid encoding."));
        box->setCurrentIndex(dataEncodingId);
    }

}
