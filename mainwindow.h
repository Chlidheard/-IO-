#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTimer>
#include <QDebug>
#include <QMap>
#include <QMutex>
#include <QLabel>
#include <QLineEdit>
#include <QTextEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QRadioButton>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QWidget>

// 编码器数据结构
struct EncoderData {
    qint32 pulse;           // 累计脉冲（有符号）
    quint32 frequency;      // 频率 (Hz)
    double angle;           // 角度 (度)
    quint16 speed;          // 转速 (RPM)
    quint16 resolution;     // 分辨率 (脉冲/圈)
};

// 命令类型枚举
enum class CmdType {
    None,
    ReadPulse,
    ReadFreq,
    ReadAngle,
    ReadSpeed,
    ResetPulse,
    SetResolution,
    SetMultiplier,
    ReadRelayStatus,
    ReadInputStatus,
    WriteRelay,
    WriteAllRelays
};

// 命令队列项
struct CommandItem {
    QByteArray command;
    QString description;
    quint8 slaveAddress;
    bool expectResponse;
    CmdType type;
    quint8 channel;
    quint8 relayNum;
    quint16 value;
    int priority;           // 1=高（编码器），2=低（IO）
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // 串口
    void on_btnOpenClose_clicked();
    void handleSerialError(QSerialPort::SerialPortError error);
    void readSerialData();
    void onCommTimeout();

    // 模式切换
    void on_comboMode_currentIndexChanged(int index);

    // 测试
    void on_btnTest_clicked();
    void onControlTimerTimeout();

    // 编码器
    void onEncoderReadTimerTimeout();
    void onEncoderResetClicked();
    void onEncoderResolutionChanged();
    void onEncoderChannelChanged(int index);
    void onReadEncoderClicked();
    void onAutoReadToggled(bool checked);

    // 命令队列
    void processCommandQueue();

private:
    // ---- UI控件指针 ----
    QComboBox   *comboPort;
    QPushButton *btnOpenClose;

    QComboBox   *comboMode;
    QLineEdit   *editRelaySingle;    // 单端口
    QLineEdit   *editRelayMulti1;
    QLineEdit   *editRelayMulti2;
    QLineEdit   *editRelayMulti3;
    QLineEdit   *editRelayMulti4;
    QLineEdit   *editInterval;
    QPushButton *btnTest;

    QRadioButton *radioInput1;
    QRadioButton *radioInput2;
    QRadioButton *radioInput3;
    QRadioButton *radioInput4;

    QComboBox   *comboEncoderChannel;
    QLabel      *labelPulse;
    QLabel      *labelFreq;
    QLabel      *labelAngle;
    QLabel      *labelSpeed;
    QLineEdit   *editResolution;
    QPushButton *btnResetPulse;
    QPushButton *btnSetResolution;
    QPushButton *btnReadEncoder;
    QCheckBox   *chkAutoRead;

    QTextEdit   *textEditLog;

    // 容器（用于模式切换显示/隐藏）
    QWidget *singleWidget;
    QWidget *multiWidget;

    // ---- 核心成员 ----
    QSerialPort *m_serialPort;

    QTimer *m_controlTimer;          // 继电器周期控制
    QTimer *m_readTimer;             // 定期读取继电器/输入状态
    QTimer *m_commTimer;             // 通信超时
    QTimer *m_portCheckTimer;        // 串口检测
    QTimer *m_encoderReadTimer;      // 编码器自动读取
    QTimer *m_commandQueueTimer;     // 命令队列处理

    bool m_isOutputActive;
    bool m_isSerialOpen;
    bool m_isTesting;
    bool m_isWaitingResponse;
    bool m_encoderAutoRead;

    quint8 m_encoderAddress;         // = 1
    quint8 m_ioAddress;              // = 2

    int m_commandCount;
    int m_errorCount;

    QMap<int, EncoderData> m_encoderData;   // key: 通道1-4
    int m_currentEncoderChannel;

    QList<CommandItem> m_commandQueue;
    QMutex m_queueMutex;
    CommandItem m_currentCommand;

    // 脉冲字节顺序
    enum class PulseEndian {
        BigEndian,
        BigEndianSwapWord,
        LittleEndian
    };
    PulseEndian m_pulseEndian;

    // ---- 私有方法 ----
    void createUI();
    void setupConnections();
    void updatePortFields();

    bool openSerialPort();
    void closeSerialPort();
    bool validateInputs();

    // IO继电器
    bool addRelayCommand(quint8 relayNum, bool state);
    void addReadRelayStatus();
    void addReadInputStatus();
    void writeRelayDefaultState();
    QStringList getActiveRelays();
    void updateInputDisplay(quint8 status);

    // 编码器
    void addReadEncoderCommand(quint8 channel, CmdType type, quint16 reg, quint16 count);
    void addResetEncoderPulse(quint8 channel);
    void addSetEncoderResolution(quint8 channel, quint16 resolution);
    void addSetEncoderMultiplier(quint8 channel, quint16 multiplier);
    void addReadAllEncoderData(quint8 channel);
    void updateEncoderDisplay();
    void setupEncoderMultiplier();

    // 队列
    void addToCommandQueue(quint8 slaveAddr, const QByteArray &command,
                           const QString &desc, CmdType type,
                           quint8 channel = 0, quint8 relayNum = 0, quint16 value = 0,
                           bool expectResponse = true, int priority = 2);

    void parseResponse(const QByteArray &data);

    void logMessage(const QString &message);
    QByteArray calculateCRC(const QByteArray &data);
};

#endif // MAINWINDOW_H