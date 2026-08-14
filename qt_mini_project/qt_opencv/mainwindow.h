#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QSerialPort>
#include <QSerialPortInfo>
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

private slots:
    void processFrame();                   // 카메라 프레임 처리 및 영상 인식
    void on_btnLedOn_clicked();            // "led on" 버튼 클릭 핸들러
    void on_pushButton_clicked();          // "led off" 버튼 클릭 핸들러
    void readSerialData();                 // STM32로부터 받는 수신 데이터(printf) 읽기

    void on_btnLedToggle_clicked();

private:
    Ui::MainWindow *ui;

    cv::VideoCapture cap;
    QTimer *timer;
    QSerialPort *serial;

    // 시리얼 명령 폭주 방지를 위한 상태 관리
    enum LedState { LED_UNKNOWN, LED_OFF_STATE, LED_ON_STATE, LED_TOGGLE_STATE };
    LedState currentLedState = LED_UNKNOWN;

    void sendLedCommand(bool turnOn);
    QImage matToQImage(const cv::Mat &mat);
};

#endif // MAINWINDOW_H
