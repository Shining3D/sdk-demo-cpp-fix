#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QtDebug>
#include <cassert>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QSharedMemory>
#include <QFileDialog>
#include <QString>
MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    m_progressDialog = new ProgressDialog(this);
	widget_step.resize(5);
	widget_step[0] = ui->widget_step1;
	widget_step[1] = ui->widget_step2;
	widget_step[2] = ui->widget_step3;
	widget_step[3] = ui->widget_step4;
	widget_step[4] = ui->widget_step5;


	m_zmqContext = zmq_ctx_new();
	auto err = zmq_strerror(zmq_errno());

	ui->checkBox_tooFlat->hide();
	ui->checkBox_trackLost->hide();
	ui->checkBox_noMarkerDetected->hide();
	ui->radioButton_Free->setChecked(true);

    m_subscriberThread = new QThread(this);
    m_subscriber = new Subscriber(this, m_zmqContext);
    m_subscriber->moveToThread(m_subscriberThread);
    connect(m_subscriberThread, &QThread::finished, m_subscriber, &QObject::deleteLater);
    connect(m_subscriber, &Subscriber::heartbeat, this, &MainWindow::onHeartbeat, Qt::QueuedConnection);
	connect(m_subscriber, &Subscriber::publishReceived, this, &MainWindow::onPublishReceived, Qt::QueuedConnection);
    m_subscriberThread->start();
	
	ui->lineEdit_PointDistance->setText("1");

	m_dataProcesserThread = new QThread(this);
	m_dataProcesser = new DataProcesser(this, m_zmqContext);
	m_dataProcesser->moveToThread(m_dataProcesserThread);
	//connect(m_dataProcesserThread, &QThread::finished, m_dataProcesser, &QObject::deleteLater);
	connect(m_dataProcesser, &DataProcesser::videoImageReady, this, &MainWindow::onVideoImageReady, Qt::QueuedConnection);
	m_dataProcesserThread->start();

	QMetaObject::invokeMethod(m_subscriber, "setup", Qt::QueuedConnection, Q_ARG(QString, "tcp://localhost:11398"));

	m_zmqReqSocket = zmq_socket(m_zmqContext, ZMQ_REQ);
    auto rc = zmq_connect(m_zmqReqSocket, "tcp://localhost:11399");
	assert(!rc);


	m_heartbeatTimer = new QTimer(this);
	m_heartbeatTimer->setInterval(210 );
	connect(m_heartbeatTimer, &QTimer::timeout, this, [&]{
		auto currentCount = ui->lcdNumber->intValue();
		if (currentCount == 0){
			m_heartbeatTimer->stop();
			QMessageBox::critical(this, "ERROR", "The platform died!");
		}
		else{
			currentCount--;
			ui->lcdNumber->display(currentCount);
		}
	});
	m_heartbeatTimer->start();

	QTimer::singleShot(0, this, [&]{
		m_progressDialog->onBeginAsync("Pulling...");
		QCoreApplication::processEvents();
		const char *sendData = "v1.0/pull";
		int nbytes = zmq_send(m_zmqReqSocket, sendData, strlen(sendData), 0);


		char buf[MAX_DATA_LENGTH * 2+ 1] = { 0 };
		nbytes = zmq_recv(m_zmqReqSocket, buf, MAX_DATA_LENGTH * 2, 0);
		auto jsonDoc = QJsonDocument::fromJson(buf);
		qDebug() << "pull results:" << jsonDoc;
		assert(!hasMore(m_zmqReqSocket));
		m_progressDialog->onFinishAsync();
		QCoreApplication::processEvents();
	});

	/*m_newProject = new NewProject();
	m_newProject->hide();
	connect(m_newProject,&NewProject::newProject, this, &MainWindow::onNewProject);*/
	connect(this, &MainWindow::newProject, this, &MainWindow::onNewProject);

	m_mesh=new mesh;
	m_save=new save;
	m_startScan=new startScan;
	m_simplify = new Simplify;
	m_bSimplify = false;
	m_reportError = new ReportError;


	connect(m_simplify, &Simplify::simplifySignal, this, &MainWindow::onSimplify);
	connect(m_startScan, &startScan::startScanSignal, this, &MainWindow::onStartScan);

	connect(m_mesh, &mesh::meshSignal, this, &MainWindow::onMesh);
	connect(m_save, &save::saveSignal, this, &MainWindow::onSave);
	ui->widget->setEnabled(false);
	ui->lineEdit_OpenProject->setEnabled(false);
	//ui->comboBox_scanExportFile->setHidden(true);

}

MainWindow::~MainWindow()
{
    delete ui;
	zmq_close(m_zmqReqSocket);
	zmq_ctx_destroy(m_zmqContext);
	m_dataProcesser->deleteLater();
}

bool MainWindow::request(const QString& cmd, const QJsonObject& jsonObj)
{
	return sendData(m_zmqReqSocket, cmd, jsonStr(jsonObj));
}

bool MainWindow::request(const QString& cmd)
{
	return sendData(m_zmqReqSocket, cmd, "");
}

bool MainWindow::hasMore(void* socket)
{
	int more = 0;
	size_t moreSize = sizeof(more);
	int rc = zmq_getsockopt(socket, ZMQ_RCVMORE, &more, &moreSize);
	qDebug() << "hasMore:" << more <<"andRc:"<<rc<< endl;

	return more != 0;
}

void MainWindow::nextStep(int num)
{
	ui->tabWidget->setCurrentIndex(num + 1);
	for (int i = 0; i < ui->tabWidget->count(); i++)
	{
		if (i != num + 1)
		{
			widget_step[i]->setEnabled(false);
		}
		else
		{
			widget_step[i]->setEnabled(true);
		}
	}

	if (ui->widget_step3->isEnabled())
	{
		ui->pushButton_Step3Back->setEnabled(true);
		ui->pushButton_Step3Next->setEnabled(true);
	}
	else
	{
		ui->pushButton_Step3Back->setEnabled(false);
		ui->pushButton_Step3Next->setEnabled(false);
	}

	if (ui->widget_step1->isEnabled())
	{
		ui->label_DeviceStatus->setText("Status");
	}
	else if (ui->widget_step2->isEnabled())
	{
		ui->label_EnterStatus->setText("Status");
	}
	else if (ui->widget_step5->isEnabled())
	{
		QString pointsNum = ui->label_pointCountR->text();
		ui->label_step5PointsNum->setText(pointsNum);
		ScanTriangleCount();
	}
}

void MainWindow::backStep(int num)
{
	ui->tabWidget->setCurrentIndex(num - 1);
	for (int i = 0; i < ui->tabWidget->count(); i++)
	{
		if (i != num - 1)
		{
			widget_step[i]->setEnabled(false);
		}
		else
		{
			widget_step[i]->setEnabled(true);
		}
	}

	if (ui->widget_step3->isEnabled())
	{
		ui->pushButton_Step3Back->setEnabled(true);
		ui->pushButton_Step3Next->setEnabled(true);
	}
	else
	{
		ui->pushButton_Step3Back->setEnabled(false);
		ui->pushButton_Step3Next->setEnabled(false);
	}

	if (ui->widget_step1->isEnabled())
	{
		ui->label_DeviceStatus->setText("Status");
	}
	else if (ui->widget_step2->isEnabled())
	{
		ui->label_EnterStatus->setText("Status");
	}

}

void MainWindow::on_pushButton_Step1Next_clicked()
{
	int index_tab = 0;
	nextStep(index_tab);
}
void MainWindow::on_pushButton_Step2Next_clicked()
{
	int index_tab = 1;
	nextStep(index_tab);
}
void MainWindow::on_pushButton_Step2Back_clicked()
{
	int index_tab = 1;
	backStep(index_tab);
}
void MainWindow::on_pushButton_Step3Next_clicked()
{
	int index_tab = 2;
	nextStep(index_tab);
}
void MainWindow::on_pushButton_Step3Back_clicked()
{
	int index_tab = 2;
	backStep(index_tab);

}
void MainWindow::on_pushButton_Step4Next_clicked()
{
	int index_tab = 3;
	nextStep(index_tab);
	if (m_reportError->isVisible())
	{
		m_reportError->m_reportError->clear();
		m_reportError->hide();
	}
}
void MainWindow::on_pushButton_Step4Back_clicked()
{
	int index_tab = 3;
	backStep(index_tab);
	if (m_reportError->isVisible())
	{
		m_reportError->m_reportError->clear();
		m_reportError->hide();
	}
}
void MainWindow::on_pushButton_Step5Back_clicked()
{
	int index_tab = 4;
	backStep(index_tab);
}

void MainWindow::on_pushButton_NewProject_clicked()
{
	int index_tab = 1;
	if ((ui->radioButton_Free->isChecked() ||
		ui->radioButton_Turnable->isChecked()) &&
		(ui->lineEdit_NewProFilePath->text().isEmpty() == false &&
		ui->lineEdit_PointDistance->text().isEmpty() == false &&
		ui->lineEdit_OpenProject->text().isEmpty()))
	{
		index_tab = 2;
	}
	else
	{
		index_tab = 1;
	}
	if (ui->radioButton_Free->isChecked())
	{
		ui->pushButton__StartFree->show();
		ui->pushButton_StartTurnable->hide();
		ui->pushButton_PauseTurnable->hide();
	}
	else if (ui->radioButton_Turnable->isChecked())
	{
		ui->pushButton__StartFree->hide();
		ui->pushButton_StartTurnable->show();
		ui->pushButton_PauseTurnable->show();
	}
	nextStep(index_tab);

	QString strPath = ui->lineEdit_NewProFilePath->text();
	QString strGlovalMarkerFile = ui->lineEdit_GlobalMarkerFileR->text();
	bool strtextureEnabled = ui->checkBox_TextureScan->isChecked();
	QString strpointDist = ui->lineEdit_PointDistance->text();
	QString stralignType = ui->comboBox__AlignType->currentText();
	bool strrapidMode = ui->checkBox_IncreseFrameRate->isChecked();
	bool strfastSave = ui->checkBox_SaveFrameImage->isChecked();
	QJsonObject jsonobject;
	jsonobject.insert("path", strPath);
	jsonobject.insert("globalMarkerFile", strGlovalMarkerFile);
	jsonobject.insert("textureEnabled", strtextureEnabled);
	jsonobject.insert("pointDist", strpointDist.toDouble());
	jsonobject.insert("alignType", stralignType);
	jsonobject.insert("rapidMode", strrapidMode);
	jsonobject.insert("fastSave", strfastSave);
	QJsonDocument document;
	document.setObject(jsonobject);
	QByteArray result = document.toJson();
	emit newProject(result);
}
void MainWindow::on_pushButton_OpenProOpen_clicked()
{
	int index_tab = 1;
	if ((ui->radioButton_Free->isChecked() ||
		ui->radioButton_Turnable->isChecked()) &&
		(ui->lineEdit_NewProFilePath->text().isEmpty() &&
		ui->lineEdit_PointDistance->text().isEmpty() &&
		ui->lineEdit_OpenProject->text().isEmpty() == false))
	{
		index_tab = 2;
	}
	else
	{
		index_tab = 1;
	}
	if (ui->radioButton_Free->isChecked())
	{
		ui->pushButton__StartFree->show();
		ui->pushButton_StartTurnable->hide();
		ui->pushButton_PauseTurnable->hide();
	}
	else if (ui->radioButton_Turnable->isChecked())
	{
		ui->pushButton__StartFree->hide();
		ui->pushButton_StartTurnable->show();
		ui->pushButton_PauseTurnable->show();
	}
	nextStep(index_tab);
}

void MainWindow::on_pushButton_DeviceCheck_clicked()
{
	const char *sendData = "v1.0/device/check";
	int nbytes = zmq_send(m_zmqReqSocket, sendData, strlen(sendData), 0);

	m_progressDialog->setWindowTitle("Check Device");

    int result = 0;
    nbytes = zmq_recv(m_zmqReqSocket, &result, sizeof(int), 0);
    qDebug() << "recv reply data:" << (result == 0? false : true);
	if (result != 0)
	{
		ui->widget->setEnabled(true);
	}
    assert(!hasMore(m_zmqReqSocket));
}


void MainWindow::on_pushButton_RegisterProcesser_clicked()
{
	if (!m_dataProcesserThread->isRunning()){
		m_dataProcesserThread->start();
	}

	//note: 防止按钮点击多次，影响接收SDK SnPlatform 的心跳包 20190701;
	ui->pushButton_RegisterProcesser->setEnabled(false);
	ui->pushButton_UnregisterProcesser->setEnabled(true);

	//m_dataProcesser->setThreadLoop(true);
	m_dataProcesser->setReqSocket(m_zmqReqSocket);
	qDebug() << "on_pushButton_RegisterProcesser_clicked" << endl;
	QMetaObject::invokeMethod(m_dataProcesser, "setup", Qt::QueuedConnection, Q_ARG(int, 12000));//12000
}

void MainWindow::on_pushButton_UnregisterProcesser_clicked()
{
	//note: 影响业务逻辑，限制只能unregister一次
	// 	if (m_zmqDataProcesserSocket) return;
	// 
	// 	m_zmqDataProcesserSocket = zmq_socket(m_zmqContext, ZMQ_REP);

	ui->pushButton_RegisterProcesser->setEnabled(true);
	ui->pushButton_UnregisterProcesser->setEnabled(false);

	const char *processUrl = "tcp://localhost:12000";
	// 	int rc = zmq_bind(m_zmqDataProcesserSocket, processUrl);
	// 	assert(!rc);
	const char * envelop = "v1.0/scan/unregister";
	auto nbytes = zmq_send(m_zmqReqSocket, envelop, strlen(envelop), ZMQ_SNDMORE);
	if (nbytes != strlen(envelop)) {
		qWarning() << "cannot send register envelop!";
		return;
	}
	nbytes = zmq_send(m_zmqReqSocket, processUrl, strlen(processUrl), 0);
	if (nbytes != strlen(processUrl)) {
		qWarning() << "cannot send register processurl!";
		return;
	}
	//
	char buf[MAX_DATA_LENGTH + 1] = { 0 };
	zmq_recv(m_zmqReqSocket, buf, MAX_DATA_LENGTH, 0);
	qDebug() << "recv : " << QString(buf);

	//m_dataProcesser->setThreadLoop(false);

	//note: 先发送“unregister”请求，然后在终止线程，否则会引起当前客户端没有调用zmq_send 回复sdk，引起sdk 数据阻塞;
	if (m_dataProcesserThread->isRunning()){
		m_dataProcesserThread->terminate();
		//m_dataProcesserThread->wait();
	}
}




int MainWindow::ScanTriangleCount()
{
	//const char *sendData = "v1.0/scan/triangleCount";
	const char *sendData = "v1.0/scan/triangleCount";
	int nbytes = zmq_send(m_zmqReqSocket, sendData, strlen(sendData), 0);
	char buf[MAX_DATA_LENGTH + 1] = { 0 };
	nbytes = zmq_recv(m_zmqReqSocket, buf, MAX_DATA_LENGTH, 0);
	int num;
	memcpy(&num, buf, sizeof(int));
	qDebug() << "scan triangleCount:" << num;
	ui->label_triangleCountR->setText(QString::number(num));
	assert(!hasMore(m_zmqReqSocket));
	return num;
}

void MainWindow::ScanFramerate()
{
	const char *sendData = "v1.0/scan/framerate";
	int nbytes = zmq_send(m_zmqReqSocket, sendData, strlen(sendData), 0);
	char buf[MAX_DATA_LENGTH + 1] = { 0 };
	nbytes = zmq_recv(m_zmqReqSocket, buf, MAX_DATA_LENGTH, 0);
	int num;
	memcpy(&num, buf, sizeof(int));
	qDebug() << "scan framerate:" << num;
	
	ui->label_frameRateR->setText(QString::number(num));
	assert(!hasMore(m_zmqReqSocket));
}


void MainWindow::ScanPointCount()
{
	const char *sendData = "v1.0/scan/pointCount";
	int nbytes = zmq_send(m_zmqReqSocket, sendData, strlen(sendData), 0);
	char buf[MAX_DATA_LENGTH + 1] = { 0 };
	nbytes = zmq_recv(m_zmqReqSocket, buf, MAX_DATA_LENGTH, 0);
	int num;
	memcpy(&num, buf, sizeof(int));
	qDebug() << "scan pointCount:" << num;
	ui->label_pointCountR->setText(QString::number(num));
	assert(!hasMore(m_zmqReqSocket));
}


void MainWindow::ScanMarkerCount()
{
	const char *sendData = "v1.0/scan/markerCount";
	int nbytes = zmq_send(m_zmqReqSocket, sendData, strlen(sendData), 0);
	char buf[MAX_DATA_LENGTH + 1] = { 0 };
	nbytes = zmq_recv(m_zmqReqSocket, buf, MAX_DATA_LENGTH, 0);
	int num;
	memcpy(&num, buf, sizeof(int));
	qDebug() << "scan markerCount:" << num;
	ui->label_markerCountR->setText(QString::number(num));
	assert(!hasMore(m_zmqReqSocket));
}

void MainWindow::ScanMeshPointCount()
{
	const char *sendData = "v1.0/scan/meshPointCount";
	int nbytes = zmq_send(m_zmqReqSocket, sendData, strlen(sendData), 0);
	char buf[MAX_DATA_LENGTH + 1] = { 0 };
	nbytes = zmq_recv(m_zmqReqSocket, buf, MAX_DATA_LENGTH, 0);
	int num;
	memcpy(&num, buf, sizeof(int));
	qDebug() << "scan meshPointCount:" << num;
	ui->label_pointCountR->setText(QString::number(num));
	assert(!hasMore(m_zmqReqSocket));
}

void MainWindow::on_pushButton_Refresh_clicked()
{
	 ScanStatus();
	 ScanTooFlat();
	 ScanTrackLost();
	 ScanNoMarkerDetected();
	 ScanDist();
	 ScanTriangleCount();
	 ScanFramerate();
	 ScanPointCount();
	 ScanMarkerCount();

	 if (m_bSimplify == true){
		 ScanMeshPointCount();
	 }
	 else{
		 ScanPointCount();
	 }
}

void MainWindow::on_pushButton_step5Refresh_clicked()
{
	ScanTriangleCount();
	if (m_bSimplify == true){
		ScanMeshPointCount();
	}
	else{
		ScanPointCount();
	}
	QString pointsNum = ui->label_pointCountR->text();
	ui->label_step5PointsNum->setText(pointsNum);
}

void MainWindow::on_pushButton__StartFree_clicked()
{
	m_progressDialog->setWindowTitle("Start");
	m_startScan->setEnable_turnableTimes(false);
	m_startScan->setAction("start");
	m_startScan->show();
	m_startScan->setSubType(1);
}

void MainWindow::on_pushButton_StartTurnable_clicked()
{
	m_progressDialog->setWindowTitle("Start");
	m_startScan->setEnable_turnableTimes(true);
	m_startScan->setAction("start");
	m_startScan->show();
	m_startScan->setSubType(2);
}

void MainWindow::on_pushButton_PauseTurnable_clicked()
{
	m_progressDialog->setWindowTitle("Pause");
	m_startScan->setAction("pause");
	m_startScan->show();
	m_startScan->setSubType(2);
}

void MainWindow::on_pushButton_Pre_clicked()
{
	m_startScan->setAction("pre");
	m_startScan->show();
	m_startScan->setSubType(0);
}

void MainWindow::on_pushButton_start_clicked()
{
	m_startScan->setAction("start");
	m_startScan->show();
	m_startScan->setSubType(0);
}

void MainWindow::on_pushButton_Pause_clicked()
{
	m_startScan->setAction("pause");
	m_startScan->show();
	m_startScan->setSubType(0);
}

void MainWindow::ScanNoMarkerDetected()
{
	const char *sendData = "v1.0/scan/noMarkerDetected";
	int nbytes = zmq_send(m_zmqReqSocket, sendData, strlen(sendData), 0);
	char buf[MAX_DATA_LENGTH + 1] = { 0 };
	nbytes = zmq_recv(m_zmqReqSocket, buf, MAX_DATA_LENGTH, 0);

	int valInt;
	memcpy(&valInt, buf, sizeof(int));
	auto valBool = valInt == 0 ? false : true;
	qDebug() << "scan noMarkerDetected:" << valBool;
	ui->checkBox_noMarkerDetected->setChecked(valBool);
	assert(!hasMore(m_zmqReqSocket));
}

void MainWindow::ScanStatus()
{
	const char *sendData = "v1.0/scan/status";
	int nbytes = zmq_send(m_zmqReqSocket, sendData, strlen(sendData), 0);
	char buf[MAX_DATA_LENGTH + 1] = { 0 };
	nbytes = zmq_recv(m_zmqReqSocket, buf, MAX_DATA_LENGTH, 0);
	qDebug() << "scan status:" << buf;
	ui->label_scanStatusR->setText(buf);
	assert(!hasMore(m_zmqReqSocket));
}

void MainWindow::on_pushButton_ScanNewProFilePath_clicked()
{
	//m_newProject->show();
	qDebug() << "New Project";
	m_progressDialog->setWindowTitle("New Project");
	QString path = QFileDialog::getSaveFileName(this, QStringLiteral("select a file"));
	ui->lineEdit_NewProFilePath->setText(path);
}

void MainWindow::on_pushButton_scanMesh_clicked()
{
	m_mesh->show();
	m_progressDialog->setWindowTitle("Mesh");
}

void MainWindow::on_pushButton_scanSave_clicked()
{
	m_progressDialog->setWindowTitle("Save");
	m_save->show();
}

void MainWindow::on_pushButton_scanSimplify_clicked()
{
	m_progressDialog->setWindowTitle("Simplify");
	m_bSimplify = true;
	m_simplify->show();
}

void MainWindow::on_pushButton_ScaneEnterScan_clicked()
{
	m_progressDialog->setWindowTitle("Enter Scan");

	const char *sendData = "v1.0/scan/enterScan";
	int nbytes = zmq_send(m_zmqReqSocket, sendData, strlen(sendData), ZMQ_SNDMORE);
	if (nbytes != strlen(sendData)) {
		qWarning() << "cannot send enterScan ZMQ_SNDMORE" << "nbytes" << nbytes;
		return;
	}
	QString str = QStringLiteral("ST_FIXED");
	nbytes = zmq_send(m_zmqReqSocket, str.toLocal8Bit().constData(), str.size(), 0);
	if (nbytes != str.size()) {
		qWarning() << "cannot send enterScan";
		return;
	}
	int result = 0;
	nbytes = zmq_recv(m_zmqReqSocket, &result, sizeof(int), 0);
	qDebug() << "enterScan recv reply data:" << (result == 0 ? false : true);
	assert(!hasMore(m_zmqReqSocket));
}

void MainWindow::onNewProject(QByteArray sendMore)
{
	char buf[MAX_DATA_LENGTH + 1] = { "v1.0/scan/newProject" };
	const char *sendData = buf;
	int nbytes = zmq_send(m_zmqReqSocket, sendData, strlen(sendData), ZMQ_SNDMORE);
	if (nbytes != strlen(sendData)) {
		qWarning() << "cannot send newProject"<<"nbytes"<< nbytes;
		return;
	}
	QString str(sendMore);
	char buf2[MAX_DATA_LENGTH + 1] = { 0 };
	QByteArray data = sendMore;
	nbytes = zmq_send(m_zmqReqSocket, data.constData(), data.size(), 0);
	if (nbytes != str.size()) {
		qWarning() << "cannot send newProject";
		return;
	}
	int result = 0;
	nbytes = zmq_recv(m_zmqReqSocket, &result, sizeof(int), 0);
	qDebug() << "onNewProject recv reply data:" << (result == 0 ? false : true);
	assert(!hasMore(m_zmqReqSocket));
}

void MainWindow::onStartScan(QByteArray sendMore)
{
	char buf[MAX_DATA_LENGTH + 1] = { "v1.0/scan/control" };
	const char *sendData = buf;
	int nbytes = zmq_send(m_zmqReqSocket, sendData, strlen(sendData), ZMQ_SNDMORE);
	if (nbytes != strlen(sendData)) {
		qWarning() << "cannot send controlScan";
		return;
	}
	qDebug() << sendMore << endl;
 	QString str(sendMore);

	QByteArray data = sendMore;
	nbytes = zmq_send(m_zmqReqSocket, data.constData(), data.size(), 0);
	if (nbytes != str.size()) {
		qWarning() << "cannot send  controlScan";
		return;
	}
	int result = 0;
	nbytes = zmq_recv(m_zmqReqSocket, &result, sizeof(int), 0);
	qDebug() << "onStartScan recv reply data:" << (result == 0 ? false : true);
	assert(!hasMore(m_zmqReqSocket));
}

void MainWindow::onEndScan(QByteArray sendMore)
{
	char buf[MAX_DATA_LENGTH + 1] = { "v1.0/scan/endScan" };
	const char *sendData = buf;
	int nbytes = zmq_send(m_zmqReqSocket, sendData, strlen(sendData), ZMQ_SNDMORE);
	if (nbytes != strlen(sendData)) {
		qWarning() << "cannot send endScan" << "nbytes" << nbytes;
		return;
	}
	QByteArray data = sendMore;
	nbytes = zmq_send(m_zmqReqSocket, data.constData(), data.size(), 0);
	if (nbytes != data.size()) {
		qWarning() << "cannot send endScan";
		return;
	}
	int result = 0;
	nbytes = zmq_recv(m_zmqReqSocket, &result, sizeof(int), 0);
	qDebug() << "onEndScan recv reply data:" << (result == 0 ? false : true);
	assert(!hasMore(m_zmqReqSocket));
}

void MainWindow::onCancelScan(QByteArray sendMore)
{
	char buf[MAX_DATA_LENGTH + 1] = { "v1.0/scan/cancelScan" };
	const char *sendData = buf;
	int nbytes = zmq_send(m_zmqReqSocket, sendData, strlen(sendData), ZMQ_SNDMORE);
	if (nbytes != strlen(sendData)) {
		qWarning() << "cannot send cancelScan";
		return;
	}
// 	QString str(sendMore);
// 	qDebug() << "onCancelScan" << str << endl;
// 	char buf2[MAX_DATA_LENGTH + 1] = { 0 };
// 	for (int i = 0; i < str.size(); i++)
// 	{
// 		buf2[i] = str.at(i).toLatin1();
// 	}
// 	const char *sendData2 = buf2;
	nbytes = zmq_send(m_zmqReqSocket, sendMore, sendMore.size(), 0);
	if (nbytes != sendMore.size()) {
		qWarning() << "cannot send cancelScan";
		return;
	}
	int result = 0;
	nbytes = zmq_recv(m_zmqReqSocket, &result, sizeof(int), 0);
	qDebug() << "onCancelScan recv reply data:" << (result == 0 ? false : true);
	assert(!hasMore(m_zmqReqSocket));
}

void MainWindow::onMesh(QByteArray sendMore)
{
	char buf[MAX_DATA_LENGTH + 1] = { "v1.0/scan/mesh" };
	const char *sendData = buf;
	int nbytes = zmq_send(m_zmqReqSocket, sendData, strlen(sendData), ZMQ_SNDMORE);
	if (nbytes != strlen(sendData)) {
		qWarning() << "cannot send mesh";
		return;
	}
// 	QString str(sendMore);
// 	char buf2[MAX_DATA_LENGTH + 1] = { 0 };
// 	for (int i = 0; i < str.size(); i++)
// 	{
// 		buf2[i] = str.at(i).toLatin1();
// 	}
// 	const char *sendData2 = buf2;
	nbytes = zmq_send(m_zmqReqSocket, sendMore, sendMore.size(), 0);
	if (nbytes != sendMore.size()) {
		qWarning() << "cannot send mesh";
		return;
	}
	int result = 0;
	nbytes = zmq_recv(m_zmqReqSocket, &result, sizeof(int), 0);
	qDebug() << "onMesh recv reply data:" << (result == 0 ? false : true);
	assert(!hasMore(m_zmqReqSocket));
}

void MainWindow::onSave(QByteArray sendMore)
{
	char buf[MAX_DATA_LENGTH + 1] = { "v1.0/scan/save" };
	const char *sendData = "v1.0/scan/save";
	int nbytes = zmq_send(m_zmqReqSocket, sendData, strlen(sendData), ZMQ_SNDMORE);
	if (nbytes != strlen(sendData)) {
		qWarning() << "cannot send onSave";
		return;
	}
// 	QString str(sendMore);
// 	char buf2[MAX_DATA_LENGTH + 1] = { 0 };
// 	for (int i = 0; i < str.size(); i++)
// 	{
// 		buf2[i] = str.at(i).toLatin1();
// 	}
// 	const char *sendData2 = buf2;
	nbytes = zmq_send(m_zmqReqSocket, sendMore, sendMore.size(), 0);
	if (nbytes != sendMore.size()) {
		qWarning() << "cannot send save";
		return;
	}
	int result = 0;
	nbytes = zmq_recv(m_zmqReqSocket, &result, sizeof(int), 0);
	qDebug() << "onSave recv reply data:" << (result == 0 ? false : true);
	assert(!hasMore(m_zmqReqSocket));
}

void MainWindow::onSimplify(QByteArray sendMore)
{
	const char *sendData = "v1.0/scan/lastSimplifyParams/set";
	int nbytes = zmq_send(m_zmqReqSocket, sendData, strlen(sendData), ZMQ_SNDMORE);
	if (nbytes != strlen(sendData)) {
		qWarning() << "cannot send scan onSimplify";
		return;
	}

	nbytes = zmq_send(m_zmqReqSocket, sendMore.constData(), strlen(sendMore.constData()), 0);
	if (nbytes != sendMore.size()) {
		qWarning() << "cannot send scan onSimplify";
		return;
	}
	int result = 0;
	nbytes = zmq_recv(m_zmqReqSocket, &result, sizeof(int), 0);
	qDebug() << "scan onSimplify recv reply data:" << (result == 0 ? false : true);
	assert(!hasMore(m_zmqReqSocket));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
	
	m_subscriberThread->quit();
	m_subscriberThread->exit(0);
	connect(m_subscriberThread, SIGNAL(finished()), m_subscriberThread, SLOT(deleteLater()));
	
	exit(0);
	MainWindow::closeEvent(event);
}

void MainWindow::on_pushButton_CancelProject_clicked()
{
	ui->lineEdit_OpenProject->clear();
}

void MainWindow::on_pushButton_ScanOpenProject_clicked()
{
	QString str = QFileDialog::getOpenFileName(this,QStringLiteral("select a file"));
	ui->lineEdit_OpenProject->setText(str);
	//qDebug() << str << endl;
	const char *sendData = "v1.0/scan/openProject";
	int nbytes = zmq_send(m_zmqReqSocket, sendData, strlen(sendData), ZMQ_SNDMORE);
	if (nbytes != strlen(sendData)) {
		qWarning() << "cannot send ScanOpenProject  ZMQ_SNDMORE " << "nbytes" << nbytes;
		return;
	}
	//QString str = ui->lineEdit_OpenProject->text();
	nbytes = zmq_send(m_zmqReqSocket, str.toLocal8Bit().constData(), str.size(), 0);
	if (nbytes != str.size()) {
		qWarning() << "cannot send ScanOpenProject";
		return;
	}
	int result = 0;
	nbytes = zmq_recv(m_zmqReqSocket, &result, sizeof(int), 0);
	qDebug() << "openProject recv reply data:" << (result == 0 ? false : true);
	assert(!hasMore(m_zmqReqSocket));
}

//void MainWindow::on_pushButton_ScanExportFile_clicked()
//{
//	const char *sendData = "v1.0/scan/exportFile";
//	int nbytes = zmq_send(m_zmqReqSocket, sendData, strlen(sendData), ZMQ_SNDMORE);
//	if (nbytes != strlen(sendData)) {
//		qWarning() << "cannot send ScanExportFile ZMQ_SNDMORE " << "nbytes" << nbytes;
//		return;
//	}
//	//QString str = ui->comboBox_scanExportFile->currentText();
//	//nbytes = zmq_send(m_zmqReqSocket, str.toLocal8Bit().constData(), str.size(), 0);
//	if (nbytes != str.size()) {
//		qWarning() << "cannot send ScanExportFile";
//		return;
//	}
//	int result = 0;
//	nbytes = zmq_recv(m_zmqReqSocket, &result, sizeof(int), 0);
//	qDebug() << "exportFile recv reply data:" << (result == 0 ? false : true);
//	assert(!hasMore(m_zmqReqSocket));
//}

void MainWindow::ScanDist()
{
	const char *sendData = "v1.0/scan/dist";
	int nbytes = zmq_send(m_zmqReqSocket, sendData, strlen(sendData), 0);
	char buf[MAX_DATA_LENGTH + 1] = { 0 };
	nbytes = zmq_recv(m_zmqReqSocket, buf, MAX_DATA_LENGTH, 0);
	int num;
	memcpy(&num, buf, sizeof(int));
	qDebug() << "scan dist:" << num;
	
	ui->label_scandistR->setText(QString::number(num));
	assert(!hasMore(m_zmqReqSocket));
}


void MainWindow::ScanTooFlat()
{
	const char *sendData = "v1.0/scan/tooFlat";
	int nbytes = zmq_send(m_zmqReqSocket, sendData, strlen(sendData), 0);
	char buf[MAX_DATA_LENGTH + 1] = { 0 };
	nbytes = zmq_recv(m_zmqReqSocket, buf, MAX_DATA_LENGTH, 0);
	
	int valInt;
	memcpy(&valInt, buf, sizeof(int));
	auto valBool = valInt == 0 ? false : true;
	qDebug() << "scan tooFlat:" << valBool;
	ui->checkBox_tooFlat->setChecked(valBool);
	assert(!hasMore(m_zmqReqSocket));
}




void MainWindow::ScanTrackLost()
{
	const char *sendData = "v1.0/scan/trackLost";
	int nbytes = zmq_send(m_zmqReqSocket, sendData, strlen(sendData), 0);
	char buf[MAX_DATA_LENGTH + 1] = { 0 };
	nbytes = zmq_recv(m_zmqReqSocket, buf, MAX_DATA_LENGTH, 0);
	
	int valInt;
	memcpy(&valInt, buf, sizeof(int));
	auto valBool = valInt == 0 ? false : true;
	qDebug() << "scan trackLost:" << valBool;
	ui->checkBox_trackLost->setChecked(valBool);
	assert(!hasMore(m_zmqReqSocket));
}

void MainWindow::on_pushButton_ScanExitScan_clicked()
{
	m_progressDialog->setWindowTitle("Exit Scan");
	const char *sendData = "v1.0/scan/exitScan";
	int nbytes = zmq_send(m_zmqReqSocket, sendData, strlen(sendData), 0);
	int result = 0;
	nbytes = zmq_recv(m_zmqReqSocket, &result, sizeof(int), 0);
	qDebug() << "exitScan recv reply data:" << (result == 0 ? false : true);
	assert(!hasMore(m_zmqReqSocket));
}




void MainWindow::onHeartbeat()
{
    m_heartbeatTimer->start();
    ui->lcdNumber->display(10);
}

void MainWindow::onPublishReceived(QString majorCmd, QString minorCmd, QByteArray data)
{
	if (majorCmd == QStringLiteral("beginAsyncAction")) {
		auto jsonObj = jsonObject(data);
		qDebug() << "beginAsyncAction json object:" << jsonObj;
		auto type = jsonObj["type"].toString();
		auto props = jsonObj["props"].toObject();

		ui->label_AsyBeginTypeR->setText(type);
		QJsonDocument document;
		document.setObject(props);
		QByteArray propsByte = document.toJson();

		ui->label_AsyBeginPropsR->setText(propsByte);
		m_progressDialog->onBeginAsync(type);
		qDebug() << "type" << type << "\n" << "props" << propsByte << endl;
		if (ui->widget_step1->isEnabled())
		{
			ui->pushButton_Step1Next->setEnabled(false);
		}
		else if (ui->widget_step2->isEnabled())
		{
			ui->pushButton_Step2Back->setEnabled(false);
			ui->pushButton_Step2Next->setEnabled(false);
		}
	}
	else if (majorCmd == QStringLiteral("finishAsyncAction")) {
		auto jsonObj = jsonObject(data);
		qDebug() << "finishAsyncAction json object:" << jsonObj;
		auto type = jsonObj["type"].toString();
		auto props = jsonObj["props"].toObject();
		auto result = jsonObj["result"].toString();
		if (props["type"] != QJsonValue::Undefined) {
			
		}
		ui->label_AsyFinishTypeR->setText(type);
		QJsonDocument document;
		document.setObject(props);
		QByteArray propsByte = document.toJson();

		ui->label_AsyFinishPropsR->setText(propsByte);

		if (ui->widget_step1->isEnabled())
		{
			ui->label_DeviceStatus->setText("Check Successful");
			ui->pushButton_Step1Next->setEnabled(true);
		}
		else if (ui->widget_step2->isEnabled())
		{
			ui->label_EnterStatus->setText("Enter Successful");
			ui->pushButton_Step2Back->setEnabled(true);
			ui->pushButton_Step2Next->setEnabled(true);
		}

		m_progressDialog->onFinishAsync();
		qDebug() << "type" << type << "\n" << "props" << propsByte << endl;
	}
	else if (majorCmd == QStringLiteral("progress")) {
		int  value = 0;
		memcpy(&value, data.constData(), data.size());
		m_progressDialog->onProgress(value);
	}
	else if (majorCmd == QStringLiteral("device")){
		if (minorCmd == QStringLiteral("firmwareUpgradable")){
			//检测到可以进行固件升级
			QMessageBox msgBox;
			msgBox.setText(QStringLiteral("Update firmware!!!"));
			msgBox.setInformativeText(QStringLiteral("Do you want to update this firmware ?"));
			msgBox.setStandardButtons(QMessageBox::Discard | QMessageBox::Ok);
			msgBox.setDefaultButton(QMessageBox::Ok);
			int ret = msgBox.exec();
		}
	}
	else if (majorCmd == QStringLiteral("scan")) {
			if (minorCmd == QStringLiteral("framerate")) {
				int num = 0;
				memcpy(&num, data.constData(), data.size());

				ui->label_frameRateR->setText(QString::number(num));
			}
			if (minorCmd == QStringLiteral("pointCount")) {
				int num = 0;
				memcpy(&num, data.constData(), data.size());
				ui->label_pointCountR->setText(QString::number(num));
			}
			if (minorCmd == QStringLiteral("markerCount")) {
				int num = 0;
				memcpy(&num, data.constData(), data.size());
				ui->label_markerCountR->setText(QString::number(num));
			}
// 			if (minorCmd == QStringLiteral("triangleCount")) {
// 				int num = 0;
// 				memcpy(&num, data.constData(), data.size());
// 				ui->label_triangleCountR->setText(QString::number(num));
// 			}
			if (minorCmd == QStringLiteral("pointFaceCount")) {
				int num = 0;
				memcpy(&num, data.constData(), data.size());
				ui->label_triangleCountR->setText(QString::number(num));
			}
			if (minorCmd == QStringLiteral("status")) {
				ui->label_scanStatusR->setText(data);
			}
			if (minorCmd == QStringLiteral("dist")) {
				int num = 0;
				memcpy(&num, data.constData(), data.size());
				ui->label_scandistR->setText(QString::number(num));
			}	
			if (minorCmd == QStringLiteral("noMarkerDetected")) {
				bool valBool = 0;
				memcpy(&valBool, data.constData(), data.size());
				ui->checkBox_noMarkerDetected->setChecked(valBool);
			}
			if (minorCmd == QStringLiteral("tooFlat")) {
				bool valBool = 0;
				memcpy(&valBool, data.constData(), data.size());
				ui->checkBox_tooFlat->setChecked(valBool);
			}
			if (minorCmd == QStringLiteral("trackLost")) {
				bool valBool = 0;
				memcpy(&valBool, data.constData(), data.size());
				ui->checkBox_trackLost->setChecked(valBool);
			}
			if (minorCmd == QStringLiteral("disconnect")) {
				QMessageBox msgBox;
				msgBox.setText(QStringLiteral("The platform is disconnect from scan service!!!"));
				msgBox.setInformativeText(QStringLiteral("Do you want to terminate operating this program ?"));
				msgBox.setStandardButtons(QMessageBox::Discard | QMessageBox::Ok);
				msgBox.setDefaultButton(QMessageBox::Ok);
				int ret = msgBox.exec();
				switch (ret) {
				case QMessageBox::Ok: {
					execTerminate();
					break;
				}
				case QMessageBox::Discard:
				default:
					break;
				}
			}
			if (ui->checkBox_tooFlat->isChecked() || ui->checkBox_trackLost->isChecked() || ui->checkBox_noMarkerDetected->isChecked())
			{
				m_reportError->show();
				if (ui->checkBox_tooFlat->isChecked() && !ui->checkBox_trackLost->isChecked() && !ui->checkBox_noMarkerDetected->isChecked())
				{
					m_reportError->m_reportError->setText("Too flat!\n");
				}
				else if (!ui->checkBox_tooFlat->isChecked() && ui->checkBox_trackLost->isChecked() && !ui->checkBox_noMarkerDetected->isChecked())
				{
					m_reportError->m_reportError->setText("Track lost!\n");
				}
				else if (!ui->checkBox_tooFlat->isChecked() && !ui->checkBox_trackLost->isChecked() && ui->checkBox_noMarkerDetected->isChecked())
				{
					m_reportError->m_reportError->setText("No marker detected!\n");
				}
				else if (ui->checkBox_tooFlat->isChecked() && ui->checkBox_trackLost->isChecked() && !ui->checkBox_noMarkerDetected->isChecked())
				{
					m_reportError->m_reportError->setText("Too flat!\nTrack lost!\n");
				}
				else if (ui->checkBox_tooFlat->isChecked() && !ui->checkBox_trackLost->isChecked() && ui->checkBox_noMarkerDetected->isChecked())
				{
					m_reportError->m_reportError->setText("Too flat!\nNo marker detected!\n");
				}
				else if (!ui->checkBox_tooFlat->isChecked() && ui->checkBox_trackLost->isChecked() && ui->checkBox_noMarkerDetected->isChecked())
				{
					m_reportError->m_reportError->setText("Track lost!\nNo marker detected!\n");
				}
				else if (ui->checkBox_tooFlat->isChecked() && ui->checkBox_trackLost->isChecked() && ui->checkBox_noMarkerDetected->isChecked())
				{
					m_reportError->m_reportError->setText("Too flat!\nTrack lost!\nNo marker detected!\n");
				}
			}
			else
			{
				m_reportError->m_reportError->clear();
				m_reportError->hide();
			}
	}
}



void MainWindow::onVideoImageReady(int camID, QPixmap pixmap)
{
	if (camID == 0){
		ui->label_Cam0->setScaledContents(true);
		ui->label_Cam0->setPixmap(pixmap);
	}
	else if (camID == 1){
		ui->label_Cam1->setScaledContents(true);
		ui->label_Cam1->setPixmap(pixmap);
	}
}

void MainWindow::execTerminate()
{
	delete ui;
	zmq_close(m_zmqReqSocket);
	zmq_ctx_destroy(m_zmqContext);
	this->close();
	QApplication::exit();

}

bool MainWindow::sendData(void* socket, const QString& cmd, const QByteArray& data)
{
	int nbytes = 0;

	auto envelop = ("v1.0/" + cmd).toLocal8Bit();
	if (data != ""){
		nbytes = zmq_send(socket, envelop.constData(), envelop.size(), ZMQ_SNDMORE);
		if (nbytes != envelop.size()){
			qCritical() << "Send envelop error! nbytes:" << nbytes;
			return false;
		}
		nbytes = zmq_send(socket, data.constData(), data.size(), 0);
		if (nbytes != data.size()){
			qCritical() << "Send data error!"
				<< " nbytes:" << nbytes
				<< " data size:" << data.size()
				<< " data : " << data;

			return false;
		}
	}
	else{
		nbytes = zmq_send(socket, envelop.constData(), envelop.size(), 0);
		if (nbytes != envelop.size()){
			qCritical() << "Send envelop error! nbytes:" << nbytes;
			return false;
		}
	}

	char buf[MAX_DATA_LENGTH + 1] = { 0 };
	nbytes = zmq_recv(socket, buf, MAX_DATA_LENGTH, 0);
	return true;
}



