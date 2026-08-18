#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDebug>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // 1. QSerialPort 객체 생성 및 포트 목록 초기화
    serial = new QSerialPort(this);

    // 콤보박스에 이벤트 필터 등록 (클릭 감지용)
    ui->cbxSelectSerialCh->installEventFilter(this);

    // 초기 포트 목록 갱신
    updateSerialPorts();

    // STM32 응답 메시지 수신 슬롯 연결
    connect(serial, &QSerialPort::readyRead, this, &MainWindow::readSerialData);

    // 2. OpenCV 웹캠 및 QTimer 설정
    cap.open(0);
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
// 이벤트 필터: 콤보박스 마우스 클릭 감지 시 리스트 갱신
// ---------------------------------------------------------
bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // 클릭 대상이 cbxSelectSerialCh 이고 마우스 버튼이 눌렸을 때
    if (watched == ui->cbxSelectSerialCh && event->type() == QEvent::MouseButtonPress) {
        // 시리얼 포트가 열려있지 않은 상태일 때만 리스트 갱신
        if (!serial->isOpen()) {
            updateSerialPorts();
        }
    }
    // 기본 이벤트 처리 유지
    return QMainWindow::eventFilter(watched, event);
}

// 시리얼 포트 목록 자동 스캔 (기존 선택 항목 유지 로직 추가)
void MainWindow::updateSerialPorts()
{
    QString currentSelected = ui->cbxSelectSerialCh->currentText();
    ui->cbxSelectSerialCh->clear();

    const auto ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &port : ports) {
        ui->cbxSelectSerialCh->addItem(port.portName());
    }

    if (ui->cbxSelectSerialCh->count() == 0) {
        ui->cbxSelectSerialCh->addItem("No Ports Found");
    } else {
        // 기존에 선택되어 있던 포트가 목록에 여전히 있다면 재선택
        int index = ui->cbxSelectSerialCh->findText(currentSelected);
        if (index != -1) {
            ui->cbxSelectSerialCh->setCurrentIndex(index);
        }
    }
}

// Connect / Disconnect 토글 버튼 이벤트
void MainWindow::on_btnConnect_clicked()
{
    if (serial->isOpen()) {
        serial->close();
        ui->btnConnect->setText("connect");
        ui->cbxSelectSerialCh->setEnabled(true);
        qDebug() << "[Serial] Port Closed.";
        return;
    }

    QString selectedPort = ui->cbxSelectSerialCh->currentText();
    if (selectedPort.isEmpty() || selectedPort == "No Ports Found") {
        qDebug() << "[Serial] Invalid Serial Port Selected!";
        return;
    }

    serial->setPortName(selectedPort);
    serial->setBaudRate(QSerialPort::Baud115200);
    serial->setDataBits(QSerialPort::Data8);
    serial->setParity(QSerialPort::NoParity);
    serial->setStopBits(QSerialPort::OneStop);

    if (serial->open(QIODevice::ReadWrite)) {
        ui->btnConnect->setText("disconnect");
        ui->cbxSelectSerialCh->setEnabled(false);
        qDebug() << "[Serial] Connected to" << selectedPort;
    } else {
        qDebug() << "[Serial] Failed to open" << selectedPort << "-" << serial->errorString();
    }
}

// 비전 처리 및 프레임 렌더링
void MainWindow::processFrame()
{
    if (!cap.isOpened() || !ui || !ui->lblImage) return;

    cv::Mat frame;
    cap >> frame;

    if (frame.empty() || frame.cols <= 0 || frame.rows <= 0) return;

    cv::Mat hsv;
    cv::cvtColor(frame, hsv, cv::COLOR_BGR2HSV);

    cv::Mat blueMask, redMask1, redMask2, redMask;
    cv::inRange(hsv, cv::Scalar(100, 120, 70), cv::Scalar(140, 255, 255), blueMask);
    cv::inRange(hsv, cv::Scalar(0, 120, 70), cv::Scalar(10, 255, 255), redMask1);
    cv::inRange(hsv, cv::Scalar(170, 120, 70), cv::Scalar(180, 255, 255), redMask2);
    redMask = redMask1 | redMask2;

    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
    cv::morphologyEx(blueMask, blueMask, cv::MORPH_OPEN, kernel);
    cv::morphologyEx(redMask, redMask, cv::MORPH_OPEN, kernel);

    const double AREA_THRESHOLD = 2000.0;
    double maxBlueArea = 0.0;
    cv::Rect blueBox;
    double maxRedArea = 0.0;
    cv::Rect redBox;

    std::vector<std::vector<cv::Point>> blueContours, redContours;
    cv::findContours(blueMask, blueContours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    for (const auto &contour : blueContours) {
        double area = cv::contourArea(contour);
        if (area > AREA_THRESHOLD && area > maxBlueArea) {
            maxBlueArea = area;
            blueBox = cv::boundingRect(contour);
        }
    }

    cv::findContours(redMask, redContours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    for (const auto &contour : redContours) {
        double area = cv::contourArea(contour);
        if (area > AREA_THRESHOLD && area > maxRedArea) {
            maxRedArea = area;
            redBox = cv::boundingRect(contour);
        }
    }

    if (maxBlueArea > 0 && maxBlueArea >= maxRedArea) {
        cv::rectangle(frame, blueBox, cv::Scalar(255, 0, 0), 2);
        cv::Point labelPos(blueBox.x, std::max(blueBox.y - 10, 20));
        cv::putText(frame, "BLUE (LED ON)", labelPos, cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 0, 0), 2);
        sendLedCommand(true);
    }
    else if (maxRedArea > 0 && maxRedArea > maxBlueArea) {
        cv::rectangle(frame, redBox, cv::Scalar(0, 0, 255), 2);
        cv::Point labelPos(redBox.x, std::max(redBox.y - 10, 20));
        cv::putText(frame, "RED (LED OFF)", labelPos, cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 0, 255), 2);
        sendLedCommand(false);
    }

    QImage img = matToQImage(frame);
    if (!img.isNull()) {
        ui->lblImage->setPixmap(QPixmap::fromImage(img).scaled(
            ui->lblImage->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
}

// 시리얼 명령 전송 및 매뉴얼 버튼
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

void MainWindow::on_btnLedOn_clicked()
{
    if (serial->isOpen()) {
        serial->write("led on\n");
        currentLedState = LED_ON_STATE;
        qDebug() << "[Manual UI] Sent: 'led on'";
    }
}

void MainWindow::on_pushButton_clicked()
{
    if (serial->isOpen()) {
        serial->write("led off\n");
        currentLedState = LED_OFF_STATE;
        qDebug() << "[Manual UI] Sent: 'led off'";
    }
}

void MainWindow::on_btnLedToggle_clicked()
{
    if (serial->isOpen()) {
        serial->write("led toggle\n");
        currentLedState = LED_TOGGLE_STATE;
        qDebug() << "[Manual UI] Sent: 'led toggle'";
    }
}

void MainWindow::on_btnServoOn_clicked()
{
    if (serial->isOpen()) {
        serial->write("servo on\n");
        qDebug() << "[Manual UI] Sent: 'servo on'";
    }
}

void MainWindow::on_btnServoOff_clicked()
{
    if (serial->isOpen()) {
        serial->write("servo off\n");
        qDebug() << "[Manual UI] Sent: 'servo off'";
    }
}

void MainWindow::on_btnCheckBlue_clicked()
{
    if (serial->isOpen()) {
        serial->write("check blue\n");
        currentLedState = LED_ON_STATE;
        qDebug() << "[Manual UI] Sent: 'check blue'";
    }
}

void MainWindow::on_btnCheckRed_clicked()
{
    if (serial->isOpen()) {
        serial->write("check red\n");
        currentLedState = LED_OFF_STATE;
        qDebug() << "[Manual UI] Sent: 'check red'";
    }
}

void MainWindow::readSerialData()
{
    QByteArray data = serial->readAll();
    qDebug() << "[STM32 Response]:" << data.trimmed();
}

QImage MainWindow::matToQImage(const cv::Mat &mat)
{
    if (mat.empty() || mat.cols <= 0 || mat.rows <= 0) return QImage();

    if (mat.type() == CV_8UC3) {
        cv::Mat rgb;
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
        return QImage(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888).copy();
    }
    else if (mat.type() == CV_8UC1) {
        return QImage(mat.data, mat.cols, mat.rows, static_cast<int>(mat.step), QImage::Format_Grayscale8).copy();
    }

    return QImage();
}