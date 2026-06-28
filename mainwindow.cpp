#include "mainwindow.h"
#include <QMessageBox>
#include <QDateTime>
#include <QThread>
#include <QScrollBar>
#include <QGridLayout>
#include <QGroupBox>

// --------------------------------------------------------------
// 构造函数 / 析构函数
// --------------------------------------------------------------
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
    m_isOutputActive(false),
    m_isSerialOpen(false),
    m_isTesting(false),
    m_isWaitingResponse(false),
    m_encoderAutoRead(true),
    m_encoderAddress(0x01),
    m_ioAddress(0x02),
    m_commandCount(0),
    m_errorCount(0),
    m_currentEncoderChannel(1),
    m_pulseEndian(PulseEndian::BigEndian)
{
    setWindowTitle("RS485 继电器+编码器控制 (编码器地址1, IO地址2)");

    // 创建界面
    createUI();
    setupConnections();

    // 串口对象
    m_serialPort = new QSerialPort(this);
    connect(m_serialPort, &QSerialPort::errorOccurred,
            this, &MainWindow::handleSerialError);
    connect(m_serialPort, &QSerialPort::readyRead,
            this, &MainWindow::readSerialData);

    // 定时器
    m_controlTimer = new QTimer(this);
    connect(m_controlTimer, &QTimer::timeout,
            this, &MainWindow::onControlTimerTimeout);

    m_readTimer = new QTimer(this);
    m_readTimer->setInterval(2000);
    connect(m_readTimer, &QTimer::timeout,
            this, &MainWindow::addReadRelayStatus);

    m_commTimer = new QTimer(this);
    m_commTimer->setSingleShot(true);
    connect(m_commTimer, &QTimer::timeout,
            this, &MainWindow::onCommTimeout);

    m_portCheckTimer = new QTimer(this);
    m_portCheckTimer->setInterval(3000);
    connect(m_portCheckTimer, &QTimer::timeout,
            this, &MainWindow::updatePortFields);

    m_encoderReadTimer = new QTimer(this);
    m_encoderReadTimer->setInterval(200);
    connect(m_encoderReadTimer, &QTimer::timeout,
            this, &MainWindow::onEncoderReadTimerTimeout);

    m_commandQueueTimer = new QTimer(this);
    m_commandQueueTimer->setInterval(20);
    connect(m_commandQueueTimer, &QTimer::timeout,
            this, &MainWindow::processCommandQueue);

    // 初始化编码器数据结构
    for (int i = 1; i <= 4; ++i) {
        EncoderData d;
        d.pulse = 0;
        d.frequency = 0;
        d.angle = 0.0;
        d.speed = 0;
        d.resolution = 2000;
        m_encoderData[i] = d;
    }

    // 默认值
    editRelaySingle->setText("1");
    editRelayMulti1->setText("1");
    editRelayMulti2->setText("2");
    editRelayMulti3->setText("3");
    editRelayMulti4->setText("4");
    editInterval->setText("1.0");
    editResolution->setText("2000");
    chkAutoRead->setChecked(true);

    // 初始化端口列表
    updatePortFields();

    logMessage("程序启动 - 编码器地址=1, IO地址=2");
}

MainWindow::~MainWindow()
{
    m_controlTimer->stop();
    m_readTimer->stop();
    m_commTimer->stop();
    m_portCheckTimer->stop();
    m_encoderReadTimer->stop();
    m_commandQueueTimer->stop();

    if (m_serialPort->isOpen()) {
        m_serialPort->clear();
        m_serialPort->close();
    }
}

// --------------------------------------------------------------
// 创建界面（纯代码）
// --------------------------------------------------------------
void MainWindow::createUI()
{
    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout *mainLayout = new QVBoxLayout(central);

    // ---- 串口控制 ----
    QHBoxLayout *portLayout = new QHBoxLayout;
    portLayout->addWidget(new QLabel("串口:"));
    comboPort = new QComboBox;
    portLayout->addWidget(comboPort);
    btnOpenClose = new QPushButton("打开串口");
    portLayout->addWidget(btnOpenClose);
    portLayout->addStretch();
    mainLayout->addLayout(portLayout);

    // ---- 继电器控制 ----
    QGroupBox *relayGroup = new QGroupBox("继电器控制");
    QVBoxLayout *relayLayout = new QVBoxLayout(relayGroup);

    // 模式与单/多端口
    QHBoxLayout *modeLayout = new QHBoxLayout;
    modeLayout->addWidget(new QLabel("模式:"));
    comboMode = new QComboBox;
    comboMode->addItems({"单端口", "多端口"});
    modeLayout->addWidget(comboMode);
    modeLayout->addStretch();
    relayLayout->addLayout(modeLayout);

    // 单端口输入
    singleWidget = new QWidget;
    QHBoxLayout *singleLayout = new QHBoxLayout(singleWidget);
    singleLayout->addWidget(new QLabel("继电器号:"));
    editRelaySingle = new QLineEdit;
    singleLayout->addWidget(editRelaySingle);
    singleLayout->addStretch();
    relayLayout->addWidget(singleWidget);

    // 多端口输入
    multiWidget = new QWidget;
    QGridLayout *multiLayout = new QGridLayout(multiWidget);
    multiLayout->addWidget(new QLabel("继电器1:"), 0, 0);
    editRelayMulti1 = new QLineEdit;
    multiLayout->addWidget(editRelayMulti1, 0, 1);
    multiLayout->addWidget(new QLabel("继电器2:"), 0, 2);
    editRelayMulti2 = new QLineEdit;
    multiLayout->addWidget(editRelayMulti2, 0, 3);
    multiLayout->addWidget(new QLabel("继电器3:"), 1, 0);
    editRelayMulti3 = new QLineEdit;
    multiLayout->addWidget(editRelayMulti3, 1, 1);
    multiLayout->addWidget(new QLabel("继电器4:"), 1, 2);
    editRelayMulti4 = new QLineEdit;
    multiLayout->addWidget(editRelayMulti4, 1, 3);
    relayLayout->addWidget(multiWidget);

    // 间隔与测试
    QHBoxLayout *testLayout = new QHBoxLayout;
    testLayout->addWidget(new QLabel("间隔(秒):"));
    editInterval = new QLineEdit;
    editInterval->setFixedWidth(60);
    testLayout->addWidget(editInterval);
    btnTest = new QPushButton("测试");
    testLayout->addWidget(btnTest);
    testLayout->addStretch();
    relayLayout->addLayout(testLayout);

    mainLayout->addWidget(relayGroup);

    // ---- 开关量输入 ----
    QGroupBox *inputGroup = new QGroupBox("开关量输入状态");
    QHBoxLayout *inputLayout = new QHBoxLayout(inputGroup);
    radioInput1 = new QRadioButton("输入1");
    radioInput2 = new QRadioButton("输入2");
    radioInput3 = new QRadioButton("输入3");
    radioInput4 = new QRadioButton("输入4");
    radioInput1->setEnabled(false);
    radioInput2->setEnabled(false);
    radioInput3->setEnabled(false);
    radioInput4->setEnabled(false);
    radioInput1->setAutoExclusive(false);
    radioInput2->setAutoExclusive(false);
    radioInput3->setAutoExclusive(false);
    radioInput4->setAutoExclusive(false);
    inputLayout->addWidget(radioInput1);
    inputLayout->addWidget(radioInput2);
    inputLayout->addWidget(radioInput3);
    inputLayout->addWidget(radioInput4);
    inputLayout->addStretch();
    mainLayout->addWidget(inputGroup);

    // ---- 编码器 ----
    QGroupBox *encGroup = new QGroupBox("编码器控制");
    QVBoxLayout *encLayout = new QVBoxLayout(encGroup);

    // 通道选择
    QHBoxLayout *chLayout = new QHBoxLayout;
    chLayout->addWidget(new QLabel("通道:"));
    comboEncoderChannel = new QComboBox;
    comboEncoderChannel->addItems({"通道1", "通道2", "通道3", "通道4"});
    chLayout->addWidget(comboEncoderChannel);
    chLayout->addStretch();
    encLayout->addLayout(chLayout);

    // 数据显示
    QGridLayout *dataLayout = new QGridLayout;
    dataLayout->addWidget(new QLabel("脉冲:"), 0, 0);
    labelPulse = new QLabel("0");
    dataLayout->addWidget(labelPulse, 0, 1);
    dataLayout->addWidget(new QLabel("频率(Hz):"), 0, 2);
    labelFreq = new QLabel("0");
    dataLayout->addWidget(labelFreq, 0, 3);
    dataLayout->addWidget(new QLabel("角度(°):"), 1, 0);
    labelAngle = new QLabel("0.00");
    dataLayout->addWidget(labelAngle, 1, 1);
    dataLayout->addWidget(new QLabel("转速(RPM):"), 1, 2);
    labelSpeed = new QLabel("0");
    dataLayout->addWidget(labelSpeed, 1, 3);
    encLayout->addLayout(dataLayout);

    // 操作按钮
    QHBoxLayout *opLayout = new QHBoxLayout;
    opLayout->addWidget(new QLabel("分辨率:"));
    editResolution = new QLineEdit;
    editResolution->setFixedWidth(60);
    opLayout->addWidget(editResolution);
    btnSetResolution = new QPushButton("设置分辨率");
    opLayout->addWidget(btnSetResolution);
    btnResetPulse = new QPushButton("清零脉冲");
    opLayout->addWidget(btnResetPulse);
    btnReadEncoder = new QPushButton("手动读取");
    opLayout->addWidget(btnReadEncoder);
    chkAutoRead = new QCheckBox("自动读取");
    opLayout->addWidget(chkAutoRead);
    opLayout->addStretch();
    encLayout->addLayout(opLayout);

    mainLayout->addWidget(encGroup);

    // ---- 日志 ----
    QGroupBox *logGroup = new QGroupBox("日志");
    QVBoxLayout *logLayout = new QVBoxLayout(logGroup);
    textEditLog = new QTextEdit;
    textEditLog->setReadOnly(true);
    logLayout->addWidget(textEditLog);
    mainLayout->addWidget(logGroup);

    // 初始可见性
    singleWidget->setVisible(true);
    multiWidget->setVisible(false);
}

// --------------------------------------------------------------
// 信号连接
// --------------------------------------------------------------
void MainWindow::setupConnections()
{
    connect(btnOpenClose, &QPushButton::clicked,
            this, &MainWindow::on_btnOpenClose_clicked);
    connect(comboMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::on_comboMode_currentIndexChanged);
    connect(btnTest, &QPushButton::clicked,
            this, &MainWindow::on_btnTest_clicked);
    connect(comboEncoderChannel, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onEncoderChannelChanged);
    connect(btnResetPulse, &QPushButton::clicked,
            this, &MainWindow::onEncoderResetClicked);
    connect(btnSetResolution, &QPushButton::clicked,
            this, &MainWindow::onEncoderResolutionChanged);
    connect(btnReadEncoder, &QPushButton::clicked,
            this, &MainWindow::onReadEncoderClicked);
    connect(chkAutoRead, &QCheckBox::toggled,
            this, &MainWindow::onAutoReadToggled);
}

// --------------------------------------------------------------
// 更新串口列表（及单/多端口可见性）
// --------------------------------------------------------------
void MainWindow::updatePortFields()
{
    // 刷新端口列表（移除了 isBusy 检测）
    if (!m_serialPort->isOpen()) {
        QString current = comboPort->currentData().toString();
        comboPort->clear();
        const auto infos = QSerialPortInfo::availablePorts();
        if (infos.isEmpty()) {
            comboPort->addItem("无可用的串口");
            comboPort->setEnabled(false);
        } else {
            for (const QSerialPortInfo &info : infos) {
                QString text = info.portName();
                if (!info.description().isEmpty())
                    text += " - " + info.description();
                // 不再显示 "[被占用]"
                comboPort->addItem(text, info.portName());
            }
            comboPort->setEnabled(true);
            int idx = comboPort->findData(current);
            if (idx >= 0) comboPort->setCurrentIndex(idx);
            else if (comboPort->count() > 0) comboPort->setCurrentIndex(0);
        }
    }

    // 单/多端口可见性
    bool single = (comboMode->currentIndex() == 0);
    singleWidget->setVisible(single);
    multiWidget->setVisible(!single);
}

// --------------------------------------------------------------
// 串口打开/关闭（移除了 isBusy 预检查）
// --------------------------------------------------------------
bool MainWindow::openSerialPort()
{
    QString selectedPort = comboPort->currentData().toString();
    if (selectedPort.isEmpty()) {
        selectedPort = comboPort->currentText();
        if (selectedPort.contains(" - "))
            selectedPort = selectedPort.split(" - ").first();
    }
    if (selectedPort.isEmpty() || selectedPort == "无可用的串口") {
        logMessage("错误：没有可用的串口");
        return false;
    }

    // 直接尝试打开，不再检查 isBusy
    if (m_serialPort->isOpen()) closeSerialPort();

    m_serialPort->setPortName(selectedPort);
    m_serialPort->setBaudRate(QSerialPort::Baud9600);
    m_serialPort->setDataBits(QSerialPort::Data8);
    m_serialPort->setParity(QSerialPort::NoParity);
    m_serialPort->setStopBits(QSerialPort::OneStop);
    m_serialPort->setFlowControl(QSerialPort::NoFlowControl);

    if (m_serialPort->open(QIODevice::ReadWrite)) {
        m_isSerialOpen = true;
        logMessage(QString("串口 %1 已打开").arg(selectedPort));
        comboPort->setEnabled(false);
        m_commandQueueTimer->start();

        // 测试通信
        addReadRelayStatus();
        addReadInputStatus();

        // 设置1倍频
        setupEncoderMultiplier();

        if (m_encoderAutoRead) {
            m_encoderReadTimer->start();
            logMessage("编码器自动读取已启动");
        }
        btnOpenClose->setText("关闭串口");
        btnTest->setEnabled(true);
        return true;
    } else {
        QString errMsg = m_serialPort->errorString();
        logMessage(QString("打开串口失败：%1").arg(errMsg));
        // 若提示被占用或权限不足，弹窗提醒
        if (errMsg.contains("Permission") || errMsg.contains("Access") ||
            errMsg.contains("in use") || errMsg.contains("busy")) {
            QMessageBox::warning(this, "串口打开失败",
                                 QString("无法打开串口 %1\n可能被其他程序占用或权限不足。").arg(selectedPort));
        }
        return false;
    }
}

void MainWindow::closeSerialPort()
{
    if (!m_serialPort->isOpen()) return;
    logMessage("正在关闭串口...");
    m_controlTimer->stop();
    m_readTimer->stop();
    m_encoderReadTimer->stop();
    m_commandQueueTimer->stop();
    m_isTesting = false;
    m_isWaitingResponse = false;

    {
        QMutexLocker locker(&m_queueMutex);
        m_commandQueue.clear();
    }

    writeRelayDefaultState();

    m_serialPort->clear();
    m_serialPort->close();
    m_isSerialOpen = false;
    comboPort->setEnabled(true);
    updatePortFields();
    btnOpenClose->setText("打开串口");
    btnTest->setEnabled(false);
    btnTest->setText("测试");
    logMessage("串口已关闭");
}

void MainWindow::on_btnOpenClose_clicked()
{
    if (m_serialPort->isOpen())
        closeSerialPort();
    else
        openSerialPort();
}

// --------------------------------------------------------------
// 命令队列（带优先级）
// --------------------------------------------------------------
void MainWindow::addToCommandQueue(quint8 slaveAddr, const QByteArray &command,
                                   const QString &desc, CmdType type,
                                   quint8 channel, quint8 relayNum, quint16 value,
                                   bool expectResponse, int priority)
{
    QMutexLocker locker(&m_queueMutex);
    CommandItem item;
    item.command = command;
    item.description = desc;
    item.slaveAddress = slaveAddr;
    item.expectResponse = expectResponse;
    item.type = type;
    item.channel = channel;
    item.relayNum = relayNum;
    item.value = value;
    item.priority = priority;
    m_commandQueue.append(item);
}

void MainWindow::processCommandQueue()
{
    if (!m_serialPort->isOpen() || m_isWaitingResponse) return;

    QMutexLocker locker(&m_queueMutex);
    if (m_commandQueue.isEmpty()) return;

    // 取最高优先级
    int bestIdx = 0;
    int bestPrio = m_commandQueue[0].priority;
    for (int i = 1; i < m_commandQueue.size(); ++i) {
        if (m_commandQueue[i].priority < bestPrio) {
            bestPrio = m_commandQueue[i].priority;
            bestIdx = i;
        }
    }
    m_currentCommand = m_commandQueue.takeAt(bestIdx);
    locker.unlock();

    // 发送
    QByteArray finalCmd = m_currentCommand.command;
    if (finalCmd.size() > 0)
        finalCmd[0] = static_cast<char>(m_currentCommand.slaveAddress);
    QByteArray noCrc = finalCmd.left(finalCmd.size() - 2);
    QByteArray newCrc = calculateCRC(noCrc);
    finalCmd.chop(2);
    finalCmd.append(newCrc);

    m_serialPort->clear(QSerialPort::Input);
    qint64 written = m_serialPort->write(finalCmd);
    if (written == -1) {
        logMessage(QString("发送失败：%1").arg(m_currentCommand.description));
        m_isWaitingResponse = false;
        return;
    }
    logMessage(QString("发送 [地址%1] %2 原始帧: %3")
                   .arg(m_currentCommand.slaveAddress)
                   .arg(m_currentCommand.description)
                   .arg(QString(finalCmd.toHex(' ').toUpper())));
    if (!m_serialPort->waitForBytesWritten(200))
        logMessage("警告：写入超时");

    if (m_currentCommand.expectResponse) {
        m_isWaitingResponse = true;
        m_commTimer->start(500);
        ++m_commandCount;
    }
}

// --------------------------------------------------------------
// 响应处理
// --------------------------------------------------------------
void MainWindow::readSerialData()
{
    QByteArray data = m_serialPort->readAll();
    if (data.isEmpty()) return;
    if (m_commTimer->isActive()) m_commTimer->stop();
    m_isWaitingResponse = false;

    logMessage(QString("接收原始帧: %1").arg(QString(data.toHex(' ').toUpper())));
    parseResponse(data);
}

void MainWindow::parseResponse(const QByteArray &data)
{
    if (data.size() < 3) {
        logMessage("无效响应（过短）");
        ++m_errorCount;
        return;
    }
    quint8 addr = static_cast<quint8>(data[0]);
    quint8 func = static_cast<quint8>(data[1]);

    if (func & 0x80) {
        quint8 err = static_cast<quint8>(data[2]);
        logMessage(QString("错误响应 [地址%1] 错误码0x%2").arg(addr).arg(err, 2, 16, QChar('0')));
        ++m_errorCount;
        return;
    }

    logMessage(QString("收到 [地址%1] 响应，功能码0x%2，当前命令类型: %3")
                   .arg(addr).arg(func, 2, 16, QChar('0'))
                   .arg(static_cast<int>(m_currentCommand.type)));

    switch (m_currentCommand.type) {
    case CmdType::ReadPulse:
        if (func == 0x03 && data.size() >= 7 && data[2] == 0x04) {
            quint8 b3 = static_cast<quint8>(data[3]);
            quint8 b4 = static_cast<quint8>(data[4]);
            quint8 b5 = static_cast<quint8>(data[5]);
            quint8 b6 = static_cast<quint8>(data[6]);
            quint32 raw = 0;
            switch (m_pulseEndian) {
            case PulseEndian::BigEndian:
                raw = (static_cast<quint32>(b3) << 24) |
                      (static_cast<quint32>(b4) << 16) |
                      (static_cast<quint32>(b5) << 8) |
                      static_cast<quint32>(b6);
                break;
            case PulseEndian::BigEndianSwapWord:
                raw = (static_cast<quint32>(b5) << 24) |
                      (static_cast<quint32>(b6) << 16) |
                      (static_cast<quint32>(b3) << 8) |
                      static_cast<quint32>(b4);
                break;
            case PulseEndian::LittleEndian:
                raw = (static_cast<quint32>(b6) << 24) |
                      (static_cast<quint32>(b5) << 16) |
                      (static_cast<quint32>(b4) << 8) |
                      static_cast<quint32>(b3);
                break;
            }
            m_encoderData[m_currentCommand.channel].pulse = static_cast<qint32>(raw);
            logMessage(QString("脉冲解析: 原始0x%1, 数值%2")
                           .arg(raw, 8, 16, QChar('0'))
                           .arg(m_encoderData[m_currentCommand.channel].pulse));
            updateEncoderDisplay();
        } else {
            logMessage("读脉冲响应格式错误");
        }
        break;

    case CmdType::ReadFreq:
        if (func == 0x03 && data.size() >= 7 && data[2] == 0x04) {
            quint32 raw = (static_cast<quint32>(static_cast<quint8>(data[3])) << 24) |
                          (static_cast<quint32>(static_cast<quint8>(data[4])) << 16) |
                          (static_cast<quint32>(static_cast<quint8>(data[5])) << 8) |
                          static_cast<quint32>(static_cast<quint8>(data[6]));
            m_encoderData[m_currentCommand.channel].frequency = raw;
            logMessage(QString("频率解析: %1 Hz").arg(m_encoderData[m_currentCommand.channel].frequency));
            updateEncoderDisplay();
        } else {
            logMessage("读频率响应格式错误");
        }
        break;

    case CmdType::ReadAngle:
        if (func == 0x03 && data.size() >= 5 && data[2] == 0x02) {
            quint16 raw = (static_cast<quint8>(data[3]) << 8) | static_cast<quint8>(data[4]);
            m_encoderData[m_currentCommand.channel].angle = raw / 100.0;
            logMessage(QString("角度解析: 原始0x%1, 数值%2°")
                           .arg(raw, 4, 16, QChar('0'))
                           .arg(m_encoderData[m_currentCommand.channel].angle, 0, 'f', 2));
            updateEncoderDisplay();
        } else {
            logMessage("读角度响应格式错误");
        }
        break;

    case CmdType::ReadSpeed:
        if (func == 0x03 && data.size() >= 5 && data[2] == 0x02) {
            quint16 raw = (static_cast<quint8>(data[3]) << 8) | static_cast<quint8>(data[4]);
            m_encoderData[m_currentCommand.channel].speed = raw;
            logMessage(QString("转速解析: %1 RPM").arg(m_encoderData[m_currentCommand.channel].speed));
            updateEncoderDisplay();
        } else {
            logMessage("读转速响应格式错误");
        }
        break;

    case CmdType::ResetPulse:
        if (func == 0x10 && data.size() >= 8) {
            logMessage(QString("通道%1脉冲清零成功").arg(m_currentCommand.channel));
            m_encoderData[m_currentCommand.channel].pulse = 0;
            updateEncoderDisplay();
        } else {
            logMessage("清零脉冲响应异常");
        }
        break;

    case CmdType::SetResolution:
        if (func == 0x06 && data.size() >= 8) {
            quint16 reg = (static_cast<quint8>(data[2]) << 8) | static_cast<quint8>(data[3]);
            quint16 val = (static_cast<quint8>(data[4]) << 8) | static_cast<quint8>(data[5]);
            if (reg == 0x0007) {
                m_encoderData[m_currentCommand.channel].resolution = val;
                logMessage(QString("通道%1分辨率设置成功：%2").arg(m_currentCommand.channel).arg(val));
                updateEncoderDisplay();
            }
        } else {
            logMessage("设置分辨率响应异常");
        }
        break;

    case CmdType::SetMultiplier:
        if (func == 0x06 && data.size() >= 8) {
            quint16 reg = (static_cast<quint8>(data[2]) << 8) | static_cast<quint8>(data[3]);
            quint16 val = (static_cast<quint8>(data[4]) << 8) | static_cast<quint8>(data[5]);
            if (reg == 0x0002) {
                logMessage(QString("倍频设置成功：%1").arg(val));
            }
        } else {
            logMessage("设置倍频响应异常");
        }
        break;

    case CmdType::WriteRelay:
        if (func == 0x05 && data.size() >= 6) {
            quint16 reg = (static_cast<quint8>(data[2]) << 8) | static_cast<quint8>(data[3]);
            quint8 relayNum = reg + 1;
            bool state = (static_cast<quint8>(data[4]) == 0xFF);
            logMessage(QString("继电器%1 %2成功").arg(relayNum).arg(state ? "开启" : "关闭"));
        } else {
            logMessage("写继电器响应异常");
        }
        break;

    case CmdType::ReadRelayStatus:
        if (func == 0x01 && data.size() >= 4) {
            quint8 status = static_cast<quint8>(data[3]);
            logMessage(QString("继电器输出状态：%1").arg(status, 8, 2, QChar('0')));
        } else {
            logMessage("读继电器状态响应异常");
        }
        break;

    case CmdType::ReadInputStatus:
        if (func == 0x02 && data.size() >= 4) {
            quint8 status = static_cast<quint8>(data[3]);
            logMessage(QString("开关量输入状态：%1 (bit0-3=端口1-4)")
                           .arg(status, 8, 2, QChar('0')));
            updateInputDisplay(status);
        } else {
            logMessage("读输入状态响应格式错误");
        }
        break;

    default:
        logMessage(QString("未处理的响应类型：%1").arg(static_cast<int>(m_currentCommand.type)));
        break;
    }
}

void MainWindow::onCommTimeout()
{
    m_isWaitingResponse = false;
    logMessage("通信超时，无响应");
    ++m_errorCount;
}

// --------------------------------------------------------------
// 继电器命令 (优先级2)
// --------------------------------------------------------------
bool MainWindow::addRelayCommand(quint8 relayNum, bool state)
{
    QByteArray cmd;
    cmd.append(static_cast<char>(0x00));
    cmd.append(static_cast<char>(0x05));
    quint16 reg = relayNum - 1;
    cmd.append(static_cast<char>((reg >> 8) & 0xFF));
    cmd.append(static_cast<char>(reg & 0xFF));
    if (state) {
        cmd.append(static_cast<char>(0xFF));
        cmd.append(static_cast<char>(0x00));
    } else {
        cmd.append(static_cast<char>(0x00));
        cmd.append(static_cast<char>(0x00));
    }
    QByteArray crc = calculateCRC(cmd);
    cmd.append(crc);

    addToCommandQueue(m_ioAddress, cmd,
                      QString("继电器%1 %2").arg(relayNum).arg(state ? "开" : "关"),
                      CmdType::WriteRelay, 0, relayNum, 0, true, 2);
    return true;
}

void MainWindow::addReadRelayStatus()
{
    QByteArray cmd;
    cmd.append(static_cast<char>(0x00));
    cmd.append(static_cast<char>(0x01));
    cmd.append(static_cast<char>(0x00));
    cmd.append(static_cast<char>(0x00));
    cmd.append(static_cast<char>(0x00));
    cmd.append(static_cast<char>(0x04));
    QByteArray crc = calculateCRC(cmd);
    cmd.append(crc);
    addToCommandQueue(m_ioAddress, cmd, "读取继电器状态",
                      CmdType::ReadRelayStatus, 0, 0, 0, true, 2);
}

void MainWindow::addReadInputStatus()
{
    QByteArray cmd;
    cmd.append(static_cast<char>(0x00));
    cmd.append(static_cast<char>(0x02));
    cmd.append(static_cast<char>(0x00));
    cmd.append(static_cast<char>(0x00));
    cmd.append(static_cast<char>(0x00));
    cmd.append(static_cast<char>(0x04));
    QByteArray crc = calculateCRC(cmd);
    cmd.append(crc);
    addToCommandQueue(m_ioAddress, cmd, "读取输入状态",
                      CmdType::ReadInputStatus, 0, 0, 0, true, 2);
}

void MainWindow::writeRelayDefaultState()
{
    for (int i = 1; i <= 4; ++i) {
        addRelayCommand(i, false);
        QThread::msleep(20);
    }
}

QStringList MainWindow::getActiveRelays()
{
    QStringList list;
    auto addIfValid = [&](const QString &text) {
        bool ok;
        int v = text.toInt(&ok);
        if (ok && v >= 1 && v <= 4) list << QString::number(v);
    };
    if (comboMode->currentIndex() == 0) {
        addIfValid(editRelaySingle->text().trimmed());
    } else {
        addIfValid(editRelayMulti1->text().trimmed());
        addIfValid(editRelayMulti2->text().trimmed());
        addIfValid(editRelayMulti3->text().trimmed());
        addIfValid(editRelayMulti4->text().trimmed());
    }
    return list;
}

void MainWindow::updateInputDisplay(quint8 status)
{
    radioInput1->setChecked(status & 0x01);
    radioInput2->setChecked(status & 0x02);
    radioInput3->setChecked(status & 0x04);
    radioInput4->setChecked(status & 0x08);
}

// --------------------------------------------------------------
// 编码器命令 (优先级1)
// --------------------------------------------------------------
void MainWindow::addReadEncoderCommand(quint8 channel, CmdType type, quint16 reg, quint16 count)
{
    QByteArray cmd;
    cmd.append(static_cast<char>(0x00));
    cmd.append(static_cast<char>(0x03));
    cmd.append(static_cast<char>((reg >> 8) & 0xFF));
    cmd.append(static_cast<char>(reg & 0xFF));
    cmd.append(static_cast<char>((count >> 8) & 0xFF));
    cmd.append(static_cast<char>(count & 0xFF));
    QByteArray crc = calculateCRC(cmd);
    cmd.append(crc);

    QString desc;
    switch (type) {
    case CmdType::ReadPulse: desc = QString("读通道%1脉冲").arg(channel); break;
    case CmdType::ReadFreq:  desc = QString("读通道%1频率").arg(channel); break;
    case CmdType::ReadAngle: desc = QString("读通道%1角度").arg(channel); break;
    case CmdType::ReadSpeed: desc = QString("读通道%1转速").arg(channel); break;
    default: desc = "读编码器";
    }
    addToCommandQueue(m_encoderAddress, cmd, desc, type, channel, 0, 0, true, 1);
}

void MainWindow::addResetEncoderPulse(quint8 channel)
{
    QByteArray cmd;
    cmd.append(static_cast<char>(0x00));
    cmd.append(static_cast<char>(0x10));
    cmd.append(static_cast<char>(0x00));
    cmd.append(static_cast<char>(0x30));
    cmd.append(static_cast<char>(0x00));
    cmd.append(static_cast<char>(0x02));
    cmd.append(static_cast<char>(0x04));
    cmd.append(static_cast<char>(0x00));
    cmd.append(static_cast<char>(0x00));
    cmd.append(static_cast<char>(0x00));
    cmd.append(static_cast<char>(0x00));
    QByteArray crc = calculateCRC(cmd);
    cmd.append(crc);
    addToCommandQueue(m_encoderAddress, cmd,
                      QString("清零通道%1脉冲").arg(channel),
                      CmdType::ResetPulse, channel, 0, 0, true, 1);
}

void MainWindow::addSetEncoderResolution(quint8 channel, quint16 resolution)
{
    QByteArray cmd;
    cmd.append(static_cast<char>(0x00));
    cmd.append(static_cast<char>(0x06));
    cmd.append(static_cast<char>(0x00));
    cmd.append(static_cast<char>(0x07));
    cmd.append(static_cast<char>((resolution >> 8) & 0xFF));
    cmd.append(static_cast<char>(resolution & 0xFF));
    QByteArray crc = calculateCRC(cmd);
    cmd.append(crc);
    addToCommandQueue(m_encoderAddress, cmd,
                      QString("设置通道%1分辨率=%2").arg(channel).arg(resolution),
                      CmdType::SetResolution, channel, 0, resolution, true, 1);
}

void MainWindow::addSetEncoderMultiplier(quint8 channel, quint16 multiplier)
{
    QByteArray cmd;
    cmd.append(static_cast<char>(0x00));
    cmd.append(static_cast<char>(0x06));
    cmd.append(static_cast<char>(0x00));
    cmd.append(static_cast<char>(0x02));
    cmd.append(static_cast<char>((multiplier >> 8) & 0xFF));
    cmd.append(static_cast<char>(multiplier & 0xFF));
    QByteArray crc = calculateCRC(cmd);
    cmd.append(crc);
    addToCommandQueue(m_encoderAddress, cmd,
                      QString("设置通道%1倍频=%2").arg(channel).arg(multiplier),
                      CmdType::SetMultiplier, channel, 0, multiplier, true, 1);
}

void MainWindow::addReadAllEncoderData(quint8 channel)
{
    addReadEncoderCommand(channel, CmdType::ReadPulse, 0x0030, 2);
    addReadEncoderCommand(channel, CmdType::ReadFreq,  0x0042, 2);
    addReadEncoderCommand(channel, CmdType::ReadAngle, 0x0044, 1);
    addReadEncoderCommand(channel, CmdType::ReadSpeed, 0x0045, 1);
}

void MainWindow::setupEncoderMultiplier()
{
    for (int ch = 1; ch <= 4; ++ch) {
        addSetEncoderMultiplier(static_cast<quint8>(ch), 1);
        QThread::msleep(20);
    }
    logMessage("已将所有编码器通道设置为1倍频");
}

void MainWindow::updateEncoderDisplay()
{
    EncoderData &d = m_encoderData[m_currentEncoderChannel];
    labelPulse->setText(QString::number(d.pulse));
    labelFreq->setText(QString::number(d.frequency) + " Hz");
    labelAngle->setText(QString::number(d.angle, 'f', 2) + "°");
    labelSpeed->setText(QString::number(d.speed) + " RPM");
    editResolution->setText(QString::number(d.resolution));
}

// --------------------------------------------------------------
// 编码器槽函数
// --------------------------------------------------------------
void MainWindow::onEncoderReadTimerTimeout()
{
    if (m_serialPort->isOpen() && m_encoderAutoRead) {
        logMessage("自动读取编码器数据");
        addReadAllEncoderData(m_currentEncoderChannel);
    }
}

void MainWindow::onEncoderResetClicked()
{
    if (!m_serialPort->isOpen()) {
        logMessage("请先打开串口");
        return;
    }
    addResetEncoderPulse(static_cast<quint8>(m_currentEncoderChannel));
}

void MainWindow::onEncoderResolutionChanged()
{
    if (!m_serialPort->isOpen()) {
        logMessage("请先打开串口");
        return;
    }
    bool ok;
    quint16 res = static_cast<quint16>(editResolution->text().toUInt(&ok));
    if (!ok || res < 1 || res > 10000) {
        logMessage("分辨率必须为1-10000");
        return;
    }
    addSetEncoderResolution(static_cast<quint8>(m_currentEncoderChannel), res);
}

void MainWindow::onEncoderChannelChanged(int idx)
{
    m_currentEncoderChannel = idx + 1;
    updateEncoderDisplay();
    if (m_serialPort->isOpen() && m_encoderAutoRead) {
        addReadAllEncoderData(m_currentEncoderChannel);
    }
}

void MainWindow::onReadEncoderClicked()
{
    if (!m_serialPort->isOpen()) {
        logMessage("请先打开串口");
        return;
    }
    logMessage("手动读取编码器数据");
    addReadAllEncoderData(m_currentEncoderChannel);
}

void MainWindow::onAutoReadToggled(bool checked)
{
    m_encoderAutoRead = checked;
    if (checked && m_serialPort->isOpen())
        m_encoderReadTimer->start();
    else
        m_encoderReadTimer->stop();
    logMessage(QString("编码器自动读取 %1").arg(checked ? "已启用" : "已停用"));
}

// --------------------------------------------------------------
// 测试按钮
// --------------------------------------------------------------
void MainWindow::on_btnTest_clicked()
{
    if (!validateInputs()) return;

    if (!m_isTesting) {
        double interval = editInterval->text().toDouble();
        int ms = static_cast<int>(interval * 1000);
        if (ms <= 0) {
            logMessage("间隔时间必须大于0");
            return;
        }

        logMessage("=== 开始测试 ===");
        logMessage(QString("模式: %1  间隔: %2秒")
                       .arg(comboMode->currentText()).arg(interval));
        if (comboMode->currentIndex() == 0)
            logMessage(QString("单端口: %1").arg(editRelaySingle->text()));
        else {
            QStringList list = getActiveRelays();
            if (!list.isEmpty()) logMessage("多端口: " + list.join(", "));
        }

        m_commandCount = 0;
        m_errorCount = 0;
        m_controlTimer->setInterval(ms);
        m_isOutputActive = true;
        onControlTimerTimeout();
        m_controlTimer->start();
        m_isTesting = true;
        btnTest->setText("停止测试");
    } else {
        m_controlTimer->stop();
        m_isTesting = false;
        writeRelayDefaultState();
        logMessage(QString("=== 测试停止 === 命令总数:%1 错误:%2")
                       .arg(m_commandCount).arg(m_errorCount));
        btnTest->setText("测试");
    }
}

void MainWindow::onControlTimerTimeout()
{
    if (!m_serialPort->isOpen()) {
        logMessage("串口断开，停止测试");
        m_controlTimer->stop();
        btnTest->setText("测试");
        m_isTesting = false;
        return;
    }

    if (m_isOutputActive) {
        logMessage("--- 开启继电器 ---");
        if (comboMode->currentIndex() == 0) {
            int relay = editRelaySingle->text().toInt();
            addRelayCommand(relay, true);
            ++m_commandCount;
        } else {
            QStringList relays = getActiveRelays();
            for (const QString &r : relays) {
                addRelayCommand(r.toInt(), true);
                ++m_commandCount;
                QThread::msleep(20);
            }
        }
    } else {
        logMessage("--- 关闭所有继电器 ---");
        writeRelayDefaultState();
        ++m_commandCount;
        addReadInputStatus();
    }

    m_isOutputActive = !m_isOutputActive;
    if (m_commandCount % 10 == 0)
        logMessage(QString("统计: 命令%1 错误%2").arg(m_commandCount).arg(m_errorCount));
}

// --------------------------------------------------------------
// 模式切换
// --------------------------------------------------------------
void MainWindow::on_comboMode_currentIndexChanged(int)
{
    updatePortFields();
}

// --------------------------------------------------------------
// 工具
// --------------------------------------------------------------
bool MainWindow::validateInputs()
{
    if (!m_serialPort->isOpen()) {
        logMessage("请先打开串口");
        return false;
    }
    if (comboMode->currentIndex() == 0) {
        bool ok;
        int v = editRelaySingle->text().toInt(&ok);
        if (!ok || v < 1 || v > 4) {
            logMessage("继电器号必须1-4");
            return false;
        }
    } else {
        if (getActiveRelays().isEmpty()) {
            logMessage("至少输入一个继电器号");
            return false;
        }
    }
    double interval = editInterval->text().toDouble();
    if (interval <= 0) {
        logMessage("间隔时间必须>0");
        return false;
    }
    return true;
}

void MainWindow::handleSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError) return;
    QString msg;
    bool fatal = false;
    switch (error) {
    case QSerialPort::DeviceNotFoundError: msg = "设备未找到"; fatal = true; break;
    case QSerialPort::PermissionError: msg = "权限错误"; fatal = true; break;
    case QSerialPort::OpenError: msg = "打开失败"; fatal = true; break;
    case QSerialPort::ResourceError: msg = "资源错误(可能被拔出)"; fatal = true; break;
    default: msg = QString("错误代码%1").arg(error);
    }
    logMessage("串口错误: " + msg);
    if (fatal && m_serialPort->isOpen()) {
        closeSerialPort();
        btnOpenClose->setText("打开串口");
        btnTest->setEnabled(false);
        m_isTesting = false;
        QMessageBox::critical(this, "串口错误",
                              "串口发生严重错误，已自动关闭。\n" + msg);
    }
}

QByteArray MainWindow::calculateCRC(const QByteArray &data)
{
    quint16 crc = 0xFFFF;
    for (int i = 0; i < data.size(); ++i) {
        crc ^= static_cast<unsigned char>(data[i]);
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x0001)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    QByteArray res;
    res.append(static_cast<char>(crc & 0xFF));
    res.append(static_cast<char>((crc >> 8) & 0xFF));
    return res;
}

void MainWindow::logMessage(const QString &msg)
{
    textEditLog->append(QDateTime::currentDateTime().toString("hh:mm:ss") + " " + msg);
    textEditLog->verticalScrollBar()->setValue(
        textEditLog->verticalScrollBar()->maximum());
}