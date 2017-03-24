/* Copyright 2016 Anastasiya Kornilova.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

#include "gyroSensor.h"

#include <qmath.h>

#include <trikKernel/configurer.h>
#include <trikKernel/timeVal.h>
#include <QsLog.h>
#include <cmath>

#include "vectorSensorWorker.h"

using namespace trikControl;

static constexpr float GYRO_250DPS = 8.75;
static constexpr double pi() { return 3.14159265358979323846;}
static constexpr float RAD_TO_MDEG = 1000 * 180 / pi();

GyroSensor::GyroSensor(const QString &deviceName, const trikKernel::Configurer &configurer
		, const trikHal::HardwareAbstractionInterface &hardwareAbstraction, VectorSensorInterface *accelerometer)
	: mState(deviceName)
	, mIsCalibrated(false)
	, mQ(QQuaternion(1, 0, 0, 0))
	, mGyroCounter(0)
	, mLastUpdate(trikKernel::TimeVal(0, 0))
	, mAccelerometer(accelerometer)
{
	mVectorSensorWorker.reset(new VectorSensorWorker(configurer.attributeByDevice(deviceName, "deviceFile"), mState
			, hardwareAbstraction, mWorkerThread));

	mBias.resize(3);
	mGyroSum.resize(3);
	mResult.resize(7);
	mRawData.resize(4);

	mAccelerometerVector.resize(3);
	mAccelerometerSum.resize(3);
	mAccelerometerCounter = 0;

	mCalibrationTimer.moveToThread(&mWorkerThread);
	mCalibrationTimer.setSingleShot(true);

	if (!mState.isFailed()) {
		qRegisterMetaType<trikKernel::TimeVal>("trikKernel::TimeVal");

		connect(mVectorSensorWorker.data(), SIGNAL(newData(QVector<int>,trikKernel::TimeVal))
				, this, SLOT(countTilt(QVector<int>,trikKernel::TimeVal)));

		connect(&mCalibrationTimer, SIGNAL(timeout()), this, SLOT(initParameters()));

		QLOG_INFO() << "Starting VectorSensor worker thread" << &mWorkerThread;

		mState.ready();
	}
}

GyroSensor::~GyroSensor()
{
	if (mWorkerThread.isRunning()) {
		QMetaObject::invokeMethod(mVectorSensorWorker.data(), "deinitialize");
		mWorkerThread.quit();
		mWorkerThread.wait();
	}
}

GyroSensor::Status GyroSensor::status() const
{
	return mState.status();
}

QVector<int> GyroSensor::read() const
{
	return mResult;
}

QVector<int> GyroSensor::readRawData() const
{
	return mRawData;
}

void GyroSensor::calibrate(int msec)
{
	qDebug() << "calib";
	mCalibrationTimer.start(msec);

	connect(mVectorSensorWorker.data(), SIGNAL(newData(QVector<int>,trikKernel::TimeVal))
			, this, SLOT(sumGyroscope(QVector<int>,trikKernel::TimeVal)));

	connect(mAccelerometer, SIGNAL(newData(QVector<int>,trikKernel::TimeVal))
			, this, SLOT(sumAccelerometer(QVector<int>,trikKernel::TimeVal)));

	mIsCalibrated = false;
}

bool GyroSensor::isCalibrated() const
{
	return mIsCalibrated;
}

void GyroSensor::countTilt(QVector<int> gyroData, trikKernel::TimeVal t)
{
	mRawData[0] = -gyroData[1];
	mRawData[1] = -gyroData[0];
	mRawData[2] = gyroData[2];
	mRawData[3] = t.packedUInt32();

	static bool timeInited = false;
	if (!timeInited) {
		timeInited = true;
		mLastUpdate = t;
	} else {

		const auto r0 = (mRawData[0] - mBias[0]) * GYRO_250DPS;
		const auto r1 = (mRawData[1] - mBias[1]) * GYRO_250DPS;
		const auto r2 = (mRawData[2] - mBias[2]) * GYRO_250DPS;

		mResult[0] = r0;
		mResult[1] = r1;
		mResult[2] = r2;
		mResult[3] = t.packedUInt32();

		constexpr auto deltaConst = pi() / 180 / 1000 / 1000000;
		const auto dt = (t - mLastUpdate) * deltaConst;

		const auto x = r0 * dt;
		const auto y = r1 * dt;
		const auto z = r2 * dt;

		const auto c1 = std::cos(z / 2);
		const auto s1 = std::sin(z / 2);
		const auto c2 = std::cos(y / 2);
		const auto s2 = std::sin(y / 2);
		const auto c3 = std::cos(x / 2);
		const auto s3 = std::sin(x / 2);

		QQuaternion deltaQ;
		deltaQ.setScalar(c1 * c2 * c3 + s1 * s2 * s3);
		deltaQ.setX(s1 * c2 * c3 - c1 * s2 * s3);
		deltaQ.setY(c1 * s2 * c3 + s1 * c2 * s3);
		deltaQ.setZ(c1 * c2 * s3 - s1 * s2 * c3);

		mQ *= deltaQ;
		mQ.normalize();

		mLastUpdate = t;

		const QVector3D euler = getEulerAngles();
		mResult[4] = euler.x();
		mResult[5] = euler.y();
		mResult[6] = euler.z();

		emit newData(mResult, t);
	}
}

void GyroSensor::initParameters	()
{
	disconnect(mVectorSensorWorker.data(), SIGNAL(newData(QVector<int>,trikKernel::TimeVal))
			, this, SLOT(sumGyroscope(QVector<int>,trikKernel::TimeVal)));

	if (mGyroCounter != 0) {
		for (int i = 0; i < 3; i++) {
			mBias[i] = mGyroSum[i] / mGyroCounter;
			mGyroSum[i] = 0;
		}
	}
	mGyroCounter = 0;

	disconnect(mAccelerometer, SIGNAL(newData(QVector<int>,trikKernel::TimeVal))
			, this, SLOT(sumAccelerometer(QVector<int>,trikKernel::TimeVal)));

	if (mAccelerometerCounter != 0) {
		for (int i = 0; i < 3; i++) {
			mAccelerometerVector[i] = mAccelerometerSum[i] / mAccelerometerCounter;
			mAccelerometerSum[i] = 0;
		}
	}
	mAccelerometerCounter = 0;

	mIsCalibrated = true;

	QVector3D acc(-mAccelerometerVector[0], -mAccelerometerVector[1], mAccelerometerVector[2]);
	acc.normalize();

	QVector3D gravity(0, 0, 1);
	float dot = QVector3D::dotProduct(acc, gravity);
	QVector3D cross = QVector3D::crossProduct(acc, gravity);

	mQ = QQuaternion::fromAxisAndAngle(cross, acos(dot) * 180 / pi());
}

void GyroSensor::sumAccelerometer(const QVector<int> &accelerometerData, const trikKernel::TimeVal &)
{
	mAccelerometerSum[0] += accelerometerData[0];
	mAccelerometerSum[1] += accelerometerData[1];
	mAccelerometerSum[2] += accelerometerData[2];

	mAccelerometerCounter++;
}

void GyroSensor::sumGyroscope(const QVector<int> &gyroData, const trikKernel::TimeVal &)
{
	mGyroSum[0] -= gyroData[1];
	mGyroSum[1] -= gyroData[0];
	mGyroSum[2] += gyroData[2];
	mGyroCounter++;
}

QVector3D GyroSensor::getEulerAngles()
{
	float pitch = 0.0;
	float roll = 0.0;
	float yaw = 0.0;

	const float x = mQ.x();
	const float y = mQ.y();
	const float z = mQ.z();
	const float w = mQ.scalar();

	const float xx = x * x;
	const float xy = x * y;
	const float xz = x * z;
	const float xw = x * w;
	const float yy = y * y;
	const float yz = y * z;
	const float yw = y * w;
	const float zz = z * z;
	const float zw = z * w;

	const float squaredLength = xx + yy + zz + w * w;
	if (!qFuzzyIsNull(squaredLength - 1.0f) && !qFuzzyIsNull(squaredLength)) {
		   xx /= squaredLength;
		   xy /= squaredLength;
		   xz /= squaredLength;
		   xw /= squaredLength;
		   yy /= squaredLength;
		   yz /= squaredLength;
		   yw /= squaredLength;
		   zz /= squaredLength;
		   zw /= squaredLength;
	   }

	   pitch = std::asin(-2.0f * (yz - xw));
	   if (pitch < M_PI_2) {
		   if (pitch > -M_PI_2) {
			   yaw = std::atan2(2.0f * (xz + yw), 1.0f - 2.0f * (xx + yy));
			   roll = std::atan2(2.0f * (xy + zw), 1.0f - 2.0f * (xx + zz));
		   } else {
			   // not a unique solution
			   roll = 0.0f;
			   yaw = -std::atan2(-2.0f * (xy - zw), 1.0f - 2.0f * (yy + zz));
		   }
	   } else {
		   // not a unique solution
		   roll = 0.0f;
		   yaw = std::atan2(-2.0f * (xy - zw), 1.0f - 2.0f * (yy + zz));
	   }

	   pitch = pitch * RAD_TO_MDEG;
	   yaw = yaw * RAD_TO_MDEG;
	   roll = roll * RAD_TO_MDEG;
	   return QVector3D(pitch, roll, yaw);
}
