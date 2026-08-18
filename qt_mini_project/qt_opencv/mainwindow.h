#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTimer>
#include <QEvent> // QEvent 헤더 추가
#include <opencv2/opencv.hpp>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    // 콤보박스 클릭 감지를 위한 이벤트 필터 재정의
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void processFrame();
    void readSerialData();
    void on_btnLedOn_clicked();
    void on_pushButton_clicked();
    void on_btnLedToggle_clicked();
    void on_btnServoOn_clicked();
    void on_btnServoOff_clicked();
    void on_btnCheckBlue_clicked();
    void on_btnCheckRed_clicked();
    void on_btnConnect_clicked();

private:
    Ui::MainWindow *ui;
    QSerialPort *serial;
    QTimer *timer;
    cv::VideoCapture cap;

    void updateSerialPorts();
    void sendLedCommand(bool turnOn);
    QImage matToQImage(const cv::Mat &mat);

    enum LedState { LED_OFF_STATE, LED_ON_STATE, LED_TOGGLE_STATE };
    LedState currentLedState = LED_OFF_STATE;
};

#endif // MAINWINDOW_H
