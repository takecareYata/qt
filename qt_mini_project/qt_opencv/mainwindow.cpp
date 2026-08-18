#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDebug>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // ---------------------------------------------------------
    // 1. QSerialPort 설정 (STM32 연동)
    // ---------------------------------------------------------
    serial = new QSerialPort(this);
    serial->setPortName("COM4"); // STM32가 연결된 포트 이름으로 수정하세요
    serial->setBaudRate(QSerialPort::Baud115200);
    serial->setDataBits(QSerialPort::Data8);
    serial->setParity(QSerialPort::NoParity);
    serial->setStopBits(QSerialPort::OneStop);

    if (serial->open(QIODevice::ReadWrite)) {
        qDebug() << "[Serial] STM32 Serial Port Opened Successfully!";
    } else {
        qDebug() << "[Serial] Failed to open Serial Port!";
    }

    // STM32 응답 메시지 수신 시 처리 슬롯 연결
    connect(serial, &QSerialPort::readyRead, this, &MainWindow::readSerialData);

    // ---------------------------------------------------------
    // 2. OpenCV 웹캠 열기 및 QTimer 시작 (30 FPS)
    // ---------------------------------------------------------
    cap.open(0); // 0번 기본 웹캠
    if (!cap.isOpened()) {
        qDebug() << "[OpenCV] Camera Open Failed!";
    }

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &MainWindow::processFrame);
    timer->start(33); // 약 30 FPS
}

MainWindow::~MainWindow()
{
    if (cap.isOpened()) cap.release();
    if (serial->isOpen()) serial->close();
    delete ui;
}

// ---------------------------------------------------------
// 3. 카메라 프레임 수신 / OpenCV 색상 분석 / UI 레이블 출력
// ---------------------------------------------------------
void MainWindow::processFrame()
{
    // 카메라가 열려있지 않거나 UI 객체가 생성되지 않았으면 즉시 리턴
    if (!cap.isOpened() || !ui || !ui->lblImage) return;

    cv::Mat frame;
    cap >> frame;

    // ★ 카메라에서 읽어온 프레임이 비어있는지 반드시 검사
    if (frame.empty() || frame.cols <= 0 || frame.rows <= 0) return;

    // BGR -> HSV 변환
    cv::Mat hsv;
    cv::cvtColor(frame, hsv, cv::COLOR_BGR2HSV);

    // 파란색 마스크 생성
    cv::Mat blueMask;
    cv::inRange(hsv, cv::Scalar(100, 120, 70), cv::Scalar(140, 255, 255), blueMask);

    // 빨간색 마스크 생성
    cv::Mat redMask1, redMask2, redMask;
    cv::inRange(hsv, cv::Scalar(0, 120, 70), cv::Scalar(10, 255, 255), redMask1);
    cv::inRange(hsv, cv::Scalar(170, 120, 70), cv::Scalar(180, 255, 255), redMask2);
    redMask = redMask1 | redMask2;

    // 노이즈 제거
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
    cv::morphologyEx(blueMask, blueMask, cv::MORPH_OPEN, kernel);
    cv::morphologyEx(redMask, redMask, cv::MORPH_OPEN, kernel);

    const double AREA_THRESHOLD = 2000.0;
    double maxBlueArea = 0.0;
    cv::Rect blueBox;

    double maxRedArea = 0.0;
    cv::Rect redBox;

    // 파란색 윤곽선 추출
    std::vector<std::vector<cv::Point>> blueContours;
    cv::findContours(blueMask, blueContours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    for (const auto &contour : blueContours) {
        double area = cv::contourArea(contour);
        if (area > AREA_THRESHOLD && area > maxBlueArea) {
            maxBlueArea = area;
            blueBox = cv::boundingRect(contour);
        }
    }

    // 빨간색 윤곽선 추출
    std::vector<std::vector<cv::Point>> redContours;
    cv::findContours(redMask, redContours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    for (const auto &contour : redContours) {
        double area = cv::contourArea(contour);
        if (area > AREA_THRESHOLD && area > maxRedArea) {
            maxRedArea = area;
            redBox = cv::boundingRect(contour);
        }
    }

    // 색상 감지 및 박스 그리기
    if (maxBlueArea > 0 && maxBlueArea >= maxRedArea) {
        cv::rectangle(frame, blueBox, cv::Scalar(255, 0, 0), 2);
        cv::Point labelPos(blueBox.x, std::max(blueBox.y - 10, 20));
        cv::putText(frame, "BLUE (LED ON)", labelPos,
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 0, 0), 2);

        sendLedCommand(true);
    }
    else if (maxRedArea > 0 && maxRedArea > maxBlueArea) {
        cv::rectangle(frame, redBox, cv::Scalar(0, 0, 255), 2);
        cv::Point labelPos(redBox.x, std::max(redBox.y - 10, 20));
        cv::putText(frame, "RED (LED OFF)", labelPos,
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 0, 255), 2);

        sendLedCommand(false);
    }

    // QImage 변환 및 UI 출력
    QImage img = matToQImage(frame);
    if (!img.isNull()) {
        ui->lblImage->setPixmap(QPixmap::fromImage(img).scaled(
            ui->lblImage->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
}

// ---------------------------------------------------------
// 4. 시리얼 명령어 전송 및 UI 버튼 이벤트 핸들러
// ---------------------------------------------------------
void MainWindow::sendLedCommand(bool turnOn)
{
    if (!serial->isOpen()) return;

    if (turnOn && currentLedState != LED_ON_STATE) {
        serial->write("check blue\n");
        currentLedState = LED_ON_STATE;
        qDebug() << "[Vision] Sent: 'check blue'";
    }
    else if (!turnOn && currentLedState != LED_OFF_STATE) {
        serial->write("check red\n");
        currentLedState = LED_OFF_STATE;
        qDebug() << "[Vision] Sent: 'check red'";
    }
}

// "led on" 버튼 (btnLedOn)
void MainWindow::on_btnLedOn_clicked()
{
    if (serial->isOpen()) {
        serial->write("led on\n");
        currentLedState = LED_ON_STATE;
        qDebug() << "[Manual UI] Sent: 'led on'";

    }
}

// "led off" 버튼 (pushButton)
void MainWindow::on_pushButton_clicked()
{
    if (serial->isOpen()) {
        serial->write("led off\n");
        currentLedState = LED_OFF_STATE;
        qDebug() << "[Manual UI] Sent: 'led off'";
    }
}

// "led toggle" 버튼 (btnLedToggle)
void MainWindow::on_btnLedToggle_clicked()
{
    if (serial->isOpen()) {
        serial->write("led toggle\n");
        currentLedState = LED_TOGGLE_STATE;
        qDebug() << "[Manual UI] Sent: 'led toggle'";
    }
}

// STM32 터미널 출력(printf) 데이터 처리
void MainWindow::readSerialData()
{
    QByteArray data = serial->readAll();
    qDebug() << "[STM32 Response]:" << data.trimmed();
}

// cv::Mat -> QImage 변환
QImage MainWindow::matToQImage(const cv::Mat &mat)
{
    // 프레임이 비어있는지 최우선 검사
    if (mat.empty() || mat.cols <= 0 || mat.rows <= 0) {
        return QImage();
    }

    if (mat.type() == CV_8UC3) {
        cv::Mat rgb;
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
        // .copy()를 통해 메모리를 안전하게 복사하여 독립적으로 유지
        return QImage(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888).copy();
    }
    else if (mat.type() == CV_8UC1) {
        return QImage(mat.data, mat.cols, mat.rows, static_cast<int>(mat.step), QImage::Format_Grayscale8).copy();
    }

    return QImage();
}
void MainWindow::on_btnServoOn_clicked()
{
    if (serial->isOpen()) {
        serial->write("servo on\n");
        currentLedState = LED_OFF_STATE;
        qDebug() << "[Manual UI] Sent: 'led toggle'";
    }
}


void MainWindow::on_btnServoOff_clicked()
{
    if (serial->isOpen()) {
        serial->write("servo off\n");
        currentLedState = LED_OFF_STATE;
        qDebug() << "[Manual UI] Sent: 'led toggle'";
    }
}


void MainWindow::on_btnCheckBlue_clicked()
{
    if (serial->isOpen()) {
        serial->write("check blue\n");
        currentLedState = LED_ON_STATE;
        qDebug() << "[Manual UI] Sent: 'led toggle'";
    }
}


void MainWindow::on_btnCheckRed_clicked()
{
    if (serial->isOpen()) {
        serial->write("check red\n");
        currentLedState = LED_OFF_STATE;
        qDebug() << "[Manual UI] Sent: 'led toggle'";
    }
}

