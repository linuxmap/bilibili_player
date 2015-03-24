#include <iostream>

#include <malloc.h>

#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <ctime>

#include <QObject>
#include <QParallelAnimationGroup>
#include <QSequentialAnimationGroup>
#include <QString>
#include <QMainWindow>
#include <QTimer>
#include <QVBoxLayout>
#include <QLabel>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsTextItem>
#include <QApplication>
#include <QPropertyAnimation>
#include <QGraphicsSvgItem>
#include <QScreen>
#include <QToolTip>
#include <QDesktopWidget>
#include <QOpenGLWidget>
#include <QGraphicsVideoItem>
#include <QSurfaceFormat>

#ifdef HAVE_KF5_WINDOWSYSTEM
#include <KWindowSystem>
#endif

#include <boost/regex.hpp>

#include "bplayer.hpp"
#include "bilibilires.hpp"


static Moving_Comments to_comments(const QDomDocument& barrage)
{
	Moving_Comments m_comments;
	// now we got 弹幕, start dumping it!

	// 先转换成好用点的格式.

	auto ds = barrage.elementsByTagName("d");

	m_comments.reserve(ds.size());

	for (int i=0; i< ds.size(); i++)
	{
		Moving_Comment c;
		auto p = ds.at(i).toElement();
		c.content = p.text().toStdString();

		auto format_string = p.attribute("p").toStdString();

		std::vector<std::string> format_string_splited;

		boost::regex_split(std::back_inserter(format_string_splited), format_string, boost::regex(","));

		c.time_stamp = boost::lexical_cast<double>(format_string_splited[0]);

		///format_string_splited[0]

		c.mode = static_cast<decltype(c.mode)>(boost::lexical_cast<int>(format_string_splited[1]));

		c.font_size = boost::lexical_cast<double>(format_string_splited[2]);

		c.font_color.setRgb(boost::lexical_cast<uint32_t>(format_string_splited[3]));

		c.post_time = boost::lexical_cast<uint64_t>(format_string_splited[4]);

		c.type = static_cast<decltype(c.type)>(boost::lexical_cast<int>(format_string_splited[5]));

		c.poster = format_string_splited[6];
		c.rowID = boost::lexical_cast<uint64_t>(format_string_splited[7]);

		m_comments.push_back(c);
	}

	std::sort(m_comments.begin(), m_comments.end(), [](const Moving_Comment& a, const Moving_Comment& b) -> bool{
		return a.time_stamp < b.time_stamp;
	});

	m_comments.capacity();

	return m_comments;
}

BPlayer::BPlayer(QObject * parent)
	: QObject(parent)
{
	play_list = new QMediaPlaylist;

	play_list->setPlaybackMode(QMediaPlaylist::Sequential);

	connect(this, SIGNAL(ZoomLevelChanged(double)), this, SLOT(adjust_window_size()));

	connect(this, SIGNAL(full_screen_mode_changed(bool)), this, SLOT(slot_full_screen_mode_changed(bool)));
}

BPlayer::~BPlayer()
{
	vplayer->stop();
	delete vplayer;
	if (m_mainwindow)
		m_mainwindow->deleteLater();
	m_mainwindow = nullptr;
}

void BPlayer::append_video_url(VideoURL url)
{
	// now we got video uri, start playing!
	QString current_url = QString::fromStdString(url.url);

	play_list->addMedia(QUrl(current_url));

	// set the title
	urls.push_back(url);
}

void BPlayer::set_barrage_dom(QDomDocument barrage)
{
	m_comments = to_comments(barrage);
	m_comment_pos = m_comments.begin();
}

void BPlayer::start_play()
{
	if (urls.empty())
		exit(1);
	// now start playing!
	m_mainwindow = new QMainWindow;

	scene = new QGraphicsScene(m_mainwindow);
	graphicsView = new QGraphicsView(scene);

	graphicsView->setFocusPolicy(Qt::NoFocus);

	graphicsView->setCacheMode(QGraphicsView::CacheNone);
	graphicsView->setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
	graphicsView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	graphicsView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	graphicsView->setContentsMargins(0,0,0,0);

	m_mainwindow->setCentralWidget(graphicsView);

	graphicsView->setBackgroundRole(QPalette::WindowText);

	QPalette palette = m_mainwindow->palette();
	palette.setColor(QPalette::Background, QColor::fromRgb(0,0,0));

	m_mainwindow->setPalette(palette);

	graphicsView->setFrameShape(QFrame::NoFrame);
	graphicsView->setFrameShadow(QFrame::Plain);

	vplayer = new QMediaPlayer;//(0, QMediaPlayer::VideoSurface);

	if (use_gl)
	{
		video_surface = new VideoItem;
		auto glwidget = new QOpenGLWidget(m_mainwindow);

		QSurfaceFormat format;
		format.setProfile(QSurfaceFormat::CompatibilityProfile);

// 		format.setRenderableType(QSurfaceFormat::OpenGLES);
// 		format.setVersion(3,3);

		format.setSwapBehavior(QSurfaceFormat::SingleBuffer);

		glwidget->setFormat(format);

		graphicsView->setViewport(glwidget);
		video_surface->setSize(QSizeF(640, 480));
		scene->addItem(video_surface);

		vplayer->setVideoOutput(video_surface);

	}else
	{
		videoItem = new QGraphicsVideoItem;
		videoItem->setSize(QSizeF(640, 480));
		scene->addItem(videoItem);

		vplayer->setVideoOutput(videoItem);
	}

	position_slide = new QSlider;

	position_slide->setOrientation(Qt::Horizontal);


	scene->addWidget(position_slide);


	vplayer->setPlaylist(play_list);
	m_mainwindow->show();

	//vplayer->show();

	QTimer::singleShot(2000, vplayer, SLOT(play()));

	vplayer->metaData("Resolution");

	vplayer->setNotifyInterval(32);

	connect(vplayer, SIGNAL(metaDataChanged(QString,QVariant)), this, SLOT(slot_metaDataChanged(QString,QVariant)));
	connect(vplayer, SIGNAL(stateChanged(QMediaPlayer::State)), this, SLOT(play_state_changed(QMediaPlayer::State)));
	connect(vplayer, SIGNAL(mediaStatusChanged(QMediaPlayer::MediaStatus)), this, SLOT(slot_mediaStatusChanged(QMediaPlayer::MediaStatus)));

	connect(play_list, SIGNAL(currentIndexChanged(int)), this, SLOT(slot_mediaChanged(int)));

	connect(vplayer, SIGNAL(positionChanged(qint64)), this, SLOT(positionChanged(qint64)));
	connect(vplayer, SIGNAL(durationChanged(qint64)), this, SLOT(durationChanged(qint64)));

	connect(position_slide, SIGNAL(sliderMoved(int)), this, SLOT(drag_slide(int)));
	connect(position_slide, SIGNAL(sliderReleased()), this, SLOT(drag_slide_done()));

	std::cout << "playing: " << play_list->currentMedia().canonicalUrl().toDisplayString().toStdString() << std::endl;

	QShortcut* shortcut = new QShortcut(QKeySequence(QKeySequence::FullScreen), m_mainwindow);
	connect(shortcut, SIGNAL(activated()), this, SLOT(toogle_full_screen_mode()));
	shortcut = new QShortcut(QKeySequence("f"), m_mainwindow);
	connect(shortcut, SIGNAL(activated()), this, SLOT(toogle_full_screen_mode()));

	shortcut = new QShortcut(QKeySequence(Qt::Key_Space), m_mainwindow);
	connect(shortcut, SIGNAL(activated()), this, SLOT(toogle_play_pause()));

	shortcut = new QShortcut(QKeySequence(QKeySequence::ZoomOut), m_mainwindow);
	connect(shortcut, SIGNAL(activated()), this, SLOT(zoom_out()));
	shortcut = new QShortcut(QKeySequence(QKeySequence::ZoomIn), m_mainwindow);
	connect(shortcut, SIGNAL(activated()), this, SLOT(zoom_in()));

	shortcut = new QShortcut(QKeySequence("Ctrl+1"), m_mainwindow);
	connect(shortcut, &QShortcut::activated, [this](){
		SetZoomLevel(1.0);
	});
	shortcut = new QShortcut(QKeySequence("Ctrl+2"), m_mainwindow);
	connect(shortcut, &QShortcut::activated, [this](){
		SetZoomLevel(2.0);
	});
	shortcut = new QShortcut(QKeySequence("Ctrl+3"), m_mainwindow);
	connect(shortcut, &QShortcut::activated, [this](){
		SetZoomLevel(3.0);
	});
	shortcut = new QShortcut(QKeySequence("Ctrl+4"), m_mainwindow);
	connect(shortcut, &QShortcut::activated, [this](){
		SetZoomLevel(4.0);
	});
	shortcut = new QShortcut(QKeySequence("Ctrl+5"), m_mainwindow);
	connect(shortcut, &QShortcut::activated, [this](){
		SetZoomLevel(5.0);
	});
	shortcut = new QShortcut(QKeySequence("Ctrl+6"), m_mainwindow);
	connect(shortcut, &QShortcut::activated, [this](){
		SetZoomLevel(6.0);
	});

	shortcut = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_Right), m_mainwindow);
	connect(shortcut, SIGNAL(activated()), this, SLOT(fast_forward()));

	shortcut = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_Left), m_mainwindow);
	connect(shortcut, SIGNAL(activated()), this, SLOT(fast_backwork()));

	video_size = QSizeF(1,1);

	adjust_window_size();
}

void BPlayer::add_barrage(const Moving_Comment& c)
{
	QFont font;
	font.setPointSizeF(c.font_size /1.5);
	font.setFamily("Sans");

	auto vsize = video_size;
	if (!qIsNaN(zoom_level))
		vsize = video_size * zoom_level;

	auto effect =  new QGraphicsDropShadowEffect();
	effect->setOffset(3);
	effect->setBlurRadius(5);
	effect->setEnabled(1);
	effect->setColor(QColor::fromRgb(0,0,0));

	QGraphicsTextItem * danmu = scene->addText(QString::fromStdString(c.content));


	danmu->setFont(font);
	danmu->setDefaultTextColor(c.font_color);

	danmu->setGraphicsEffect(effect);

	auto preferedY = lastY += danmu->boundingRect().height() + graphicsView->logicalDpiY() / 72.0 * 3 ;

	auto textWidth = danmu->boundingRect().width();

	if ( lastY > vsize.height()*0.66)
		lastY = 0;

	if (use_bullet)
	{
		QTransform qtrans;
		qtrans.translate(vsize.width(), preferedY);
		danmu->setTransform(qtrans);
		m_danmumgr.add_danmu(danmu);
		return;
	}

	if ( lastY > vsize.height() * 0.22)
	{
		// 应该开始寻找替代位置
		for (int guessY = 6; guessY < vsize.height() * 0.7 ; guessY++)
		{
			QRect rect(vsize.width() - textWidth * 0.7, guessY, textWidth, danmu->boundingRect().height() + graphicsView->logicalDpiY() / 72.0 * 2);
			auto items = graphicsView->items(rect, Qt::IntersectsItemShape);

			items.removeAll(videoItem);
			items.removeAll(video_surface);

			if (items.empty())
			{
				preferedY = guessY;
				break;
			}else if (items.size() == 1)
			{
				if (items.at(0) == danmu)
				{
					preferedY = guessY;
					break;
				}
			}

			auto bottom_item_it  = std::max_element(items.begin(), items.end(), [](QGraphicsItem* itema,QGraphicsItem* itemb){
				return itema->boundingRect().bottom() < itemb->boundingRect().bottom();
			});

			if (bottom_item_it != items.end())
			{
				lastY = (*bottom_item_it)->boundingRect().bottom();
			}
		}
	}


	QTransform qtrans;
	qtrans.translate(vsize.width(), preferedY);
	danmu->setTransform(qtrans);

	QVariantAnimation *animation = new QVariantAnimation(danmu);
	connect(animation, SIGNAL(finished()), danmu, SLOT(deleteLater()));

	animation->setStartValue(vsize.width());
	animation->setEndValue((qreal)0.0 - textWidth);
	animation->setDuration(vsize.width() * 6);

	connect(animation, &QVariantAnimation::valueChanged, danmu, [danmu](const QVariant& v)
	{
		auto dy = danmu->transform().dy();

		QTransform qtrans;

		qtrans.translate(v.toReal(), dy);

		danmu->setTransform(qtrans);
	});
	animation->start();
}

void BPlayer::drag_slide(int p)
{
	_drag_positoin = p;

	auto current_play_time = QString("%1:%2:%3").arg(((_drag_positoin/1000)/60)/60)
		.arg(QString::fromStdString(std::to_string(((_drag_positoin/1000)/60) % 60)), 2, QChar('0'))
		.arg(QString::fromStdString(std::to_string((_drag_positoin/1000) % 60)), 2, QChar('0'));

	QToolTip::hideText();
	QToolTip::showText(QCursor::pos(), current_play_time);
}

void BPlayer::drag_slide_done()
{
	if (_drag_positoin != -1)
	{
		auto result = map_position_to_media(_drag_positoin);
		if (result.first == play_list->currentIndex())
			vplayer->setPosition(result.second);
		else
		{
			vplayer->stop();
			play_list->setCurrentIndex(result.first);
			vplayer->setPosition(result.second);
			vplayer->play();
		}

		// 移动弹幕位置.
		m_comment_pos = m_comments.begin();

		while (m_comment_pos != m_comments.end())
		{
			const Moving_Comment & c = * m_comment_pos;
			if (c.time_stamp > _drag_positoin / 1000.0)
				break;
			m_comment_pos ++;
		}

	}
	_drag_positoin = -1;
}

void BPlayer::fast_backwork()
{
	_drag_positoin = position_slide->value() - 30000;
	drag_slide_done();
}

void BPlayer::fast_forward()
{
	_drag_positoin = position_slide->value() + 30000;
	drag_slide_done();
}

void BPlayer::positionChanged(qint64 position)
{
	quint64 real_pos = map_position_from_media(position);
	if (_drag_positoin == -1)
		position_slide->setValue(real_pos);

	// 更改 tooltip

	auto current_play_time = QString("%1:%2:%3").arg(((real_pos/1000)/60)/60)
		.arg(QString::fromStdString(std::to_string(((real_pos/1000)/60) % 60)), 2, QChar('0'))
		.arg(QString::fromStdString(std::to_string((real_pos/1000) % 60)), 2, QChar('0'));

	bool tooltip_changed = position_slide->toolTip() != current_play_time;
	position_slide->setToolTip(current_play_time);

	if (tooltip_changed && position_slide->underMouse())
	{
		QToolTip::hideText();

// 		QPoint tooltip_pos = m_mainwindow->mapFromGlobal(QCursor::pos());
		QToolTip::showText(QCursor::pos(), position_slide->toolTip());
	}


	// 播放弹幕.

	double time_stamp = real_pos / 1000.0;

	while (m_comment_pos != m_comments.end())
	{
		const Moving_Comment & c = * m_comment_pos;
		if (c.time_stamp < time_stamp)
		{
			m_comment_pos ++;

			// 添加弹幕.

			if (c.type ==0)
			{
				add_barrage(c);
			}

		}else
			break;
	}
}

void BPlayer::durationChanged(qint64 duration)
{
	if (urls.size() > 1)
	{
		duration = std::accumulate(urls.begin(), urls.end(), 0, [](qint64 d, const VideoURL& u){
			return d + u.duration;
		});
	}

	position_slide->setRange(0, duration);
}

void BPlayer::adjust_window_size()
{
	auto widget_size = video_size;
	if (!qIsNaN(zoom_level))
		widget_size = video_size * zoom_level;

	if (videoItem)
		videoItem->setSize(widget_size);

	if (video_surface)
		video_surface->setSize(widget_size);

// 	video_surface->setX(80);

	position_slide->setGeometry(0, widget_size.height(), widget_size.width(), position_slide->geometry().height());

	QSizeF player_visiable_area_size = widget_size;

	player_visiable_area_size.rheight() += position_slide->geometry().height();

	graphicsView->setFixedSize(player_visiable_area_size.toSize());

	auto adjusted_size = graphicsView->minimumSizeHint();


	scene->setSceneRect(QRectF(QPointF(), player_visiable_area_size));

	m_danmumgr.video_width = video_size.width();

	if (!m_mainwindow->isFullScreen())
		m_mainwindow->adjustSize();
}

void BPlayer::zoom_in()
{
	if (zoom_level < 8)
		SetZoomLevel(zoom_level + 1.0);
	if(zoom_level >= 8.0)
		zoom_level = 8.0;
}

void BPlayer::zoom_out()
{
	if(zoom_level >= 2.0)
		SetZoomLevel(zoom_level - 1.0);
	if(zoom_level <= 1.0)
		zoom_level = 1.0;
}

void BPlayer::toogle_full_screen_mode()
{
	m_mainwindow->setWindowState(m_mainwindow->windowState() ^ Qt::WindowFullScreen);

	full_screen_mode_changed(full_screen_mode());
}

void BPlayer::set_full_screen_mode(bool v)
{
	if (v)
		m_mainwindow->setWindowState(m_mainwindow->windowState() | Qt::WindowFullScreen);
	else
		m_mainwindow->setWindowState(m_mainwindow->windowState() & ~Qt::WindowFullScreen);

	full_screen_mode_changed(v);
}

void BPlayer::slot_full_screen_mode_changed(bool)
{
#ifdef HAVE_KF5_WINDOWSYSTEM
	KWindowSystem::setBlockingCompositing(m_mainwindow->winId(), m_mainwindow->isFullScreen());
#endif

	if (m_mainwindow->isFullScreen())
	{
		position_slide->hide();
		m_mainwindow->setCursor(Qt::BlankCursor);
		graphicsView->setCursor(Qt::BlankCursor);
		if (videoItem)
			videoItem->setCursor(Qt::BlankCursor);
	}
	else
	{
		position_slide->show();
		m_mainwindow->unsetCursor();
		graphicsView->unsetCursor();

		if (videoItem){
			videoItem->unsetCursor();
			videoItem->setCursor(Qt::ArrowCursor);
		}
		m_mainwindow->setCursor(Qt::ArrowCursor);
		graphicsView->setCursor(Qt::ArrowCursor);

// 		adjust_window_size();
	}
}

void BPlayer::slot_metaDataChanged(QString key, QVariant v)
{
	if (key == "Resolution")
	{
		// 计算比例。

		if(VideoAspect=="auto")
		{
			video_size = v.toSize();
		}
		else
		{
			int w=0,h=0;
			std::sscanf(VideoAspect.toStdString().c_str(),  "%d:%d", &w,&h);

			QSizeF templatesize(w,h);

			templatesize.scale(v.toSizeF(), Qt::KeepAspectRatioByExpanding);

			video_size = templatesize;

		}

		if (qIsNaN(zoom_level))
		{
			// 根据屏幕大小决定默认的缩放比例.
			QSize desktopsize = qApp->desktop()->availableGeometry().size();

			zoom_level = qMin(
				desktopsize.height() / video_size.height()
				,
				desktopsize.width() / video_size.width()
			);

			if (zoom_level <= 1.0)
				zoom_level = 1.0;
			else
				zoom_level = (long)(zoom_level);

			ZoomLevelChanged(zoom_level);
		}

	}
}

void BPlayer::slot_mediaChanged(int)
{
	std::cout << "playing: " << play_list->currentMedia().canonicalUrl().toDisplayString().toStdString() << std::endl;

}

std::pair< int, qint64 > BPlayer::map_position_to_media(qint64 pos)
{
	int media_index = 0;

	if (urls.size() == 1)
	{
		return std::make_pair(media_index, pos);
	}

	for (; media_index < urls.size(); media_index++)
	{
		const VideoURL & url = urls[media_index];
		if (pos > url.duration)
		{
			pos -= url.duration;
		}else
		{
			return std::make_pair(media_index, pos);
		}
	}

	return std::make_pair(media_index, pos);
}


qint64 BPlayer::map_position_from_media(qint64 pos)
{
	if (urls.size() == 1)
		return pos;

	if (play_list->currentIndex() == 0)
		return pos;

	for (int i = 0; i < play_list->currentIndex(); i++)
	{
		pos += urls[i].duration;
	}
	return pos;
}
void BPlayer::toogle_play_pause()
{
	switch (vplayer->state())
	{
		case QMediaPlayer::PlayingState:
		{
			vplayer->pause();

			// display 一个 pause 图标

			if (pause_indicator)
				pause_indicator->deleteLater();

			QGraphicsSvgItem * svg_item = new QGraphicsSvgItem("://res/pause.svg");
			pause_indicator = svg_item;

			scene->addItem(pause_indicator);

			auto effect = new QGraphicsOpacityEffect;

			pause_indicator->setGraphicsEffect(effect);

			auto ani_group = new QParallelAnimationGroup(svg_item);
			connect(ani_group, SIGNAL(finished()), ani_group, SLOT(deleteLater()));

			auto ani = new QPropertyAnimation(effect, "opacity", ani_group);
			auto ani_4 = new QPropertyAnimation(svg_item, "scale", ani_group);

			connect(ani_4, &QVariantAnimation::valueChanged, svg_item, [svg_item, this](const QVariant & value)
			{
				pause_indicator->setX(video_size.width() * zoom_level / 2 - (svg_item->boundingRect().size() * value.toReal()).width()/2);
				pause_indicator->setY(video_size.height() * zoom_level / 2 - (svg_item->boundingRect().size() * value.toReal()).height()/2);
			});

			ani_group->addAnimation(ani);
			ani_group->addAnimation(ani_4);
			ani_4->setDuration(330);
			ani_4->setStartValue(3.0);
			ani_4->setEndValue(1.0);
			ani_4->setEasingCurve(QEasingCurve::OutBack);

			ani->setDuration(800);

			ani->setStartValue(0.0);
			ani->setEndValue(0.7);

			pause_indicator->show();

			ani_group->start();

			malloc_trim(0);

			break;
		}
		case QMediaPlayer::PausedState:
		{
			vplayer->play();

			if (pause_indicator)
				pause_indicator->deleteLater();
			if (play_indicator)
				play_indicator->deleteLater();

			QGraphicsSvgItem * svg_item = new QGraphicsSvgItem("://res/play.svg");
			play_indicator = svg_item;

			scene->addItem(play_indicator);

			auto effect = new QGraphicsOpacityEffect;

			play_indicator->setGraphicsEffect(effect);

			auto ani_group = new QSequentialAnimationGroup(svg_item);

			auto ani_1 = new QPropertyAnimation(effect, "opacity", ani_group);

			ani_1->setDuration(50);

			ani_1->setStartValue(0.0);
			ani_1->setEndValue(0.7);

			auto ani_2 = new QPropertyAnimation(effect, "opacity", ani_group);

			ani_2->setDuration(300);

			ani_2->setStartValue(0.7);
			ani_2->setEndValue(0.6);

			auto ani_group_2 = new QParallelAnimationGroup(svg_item);

			auto ani_3 = new QPropertyAnimation(effect, "opacity", ani_group_2);
			auto ani_4 = new QPropertyAnimation(svg_item, "scale", ani_group_2);

			connect(ani_4, &QVariantAnimation::valueChanged, svg_item, [svg_item, this](const QVariant & value)
			{
				play_indicator->setX(video_size.width() * zoom_level / 2 - (svg_item->boundingRect().size() * value.toReal()).width()/2);
				play_indicator->setY(video_size.height() * zoom_level / 2 - (svg_item->boundingRect().size() * value.toReal()).height()/2);
			});

			ani_3->setDuration(750);
			ani_3->setStartValue(0.6);
			ani_3->setEndValue(0.0);

			ani_4->setDuration(880);
			ani_4->setStartValue(1.0);
			ani_4->setEndValue(3.2);
			ani_4->setEasingCurve(QEasingCurve::InBack);

			ani_group->addAnimation(ani_1);
			ani_group->addAnimation(ani_2);

			ani_group_2->addAnimation(ani_3);
			ani_group_2->addAnimation(ani_4);

			ani_group->addAnimation(ani_group_2);

			connect(ani_group, SIGNAL(finished()), ani_group, SLOT(deleteLater()), Qt::QueuedConnection);

			play_indicator->setX(video_size.width() * zoom_level / 2 - svg_item->boundingRect().size().width()/2);
			play_indicator->setY(video_size.height() * zoom_level / 2 - svg_item->boundingRect().size().height()/2);

			play_indicator->show();

			ani_group->start();

			break;
		}
	}
}

void BPlayer::play_state_changed(QMediaPlayer::State state)
{
	switch(state)
	{
		case QMediaPlayer::PlayingState:
		{
			m_screesave_inhibitor.reset(new ScreenSaverInhibitor("bilibili-player", "playing videos"));
			m_mainwindow->raise();
			break;
		}
		case QMediaPlayer::PausedState:
		case QMediaPlayer::StoppedState:
		{
			m_screesave_inhibitor.reset();
		}
	}
}

void BPlayer::slot_mediaStatusChanged(QMediaPlayer::MediaStatus status)
{

	qDebug() << "media state :" << status;

	switch(status)
	{
		case QMediaPlayer::BufferedMedia:
// 			qDebug() << "media is buffering";;
// 			position_slide->setEnabled(true);

			// 在这里取消缓冲滚动圈圈

 			break;
		case QMediaPlayer::StalledMedia:
		{
			// 显示一个缓冲的圈圈



			// 试试看切换到备用 url
			auto cindex = play_list->currentIndex();
			VideoURL& url = urls[play_list->currentIndex()];

// 			qDebug() << "media stalled";

			return;

			// TODO 看来能正常下载也是会发出几次 stalled 回调的
			if (url.backup_urls.size() >= 1)
			{
				play_list->insertMedia(play_list->nextIndex(), QUrl(QString::fromStdString(url.backup_urls[0])));
				play_list->removeMedia(cindex);

				// 删掉已经使用的备用 url
				url.backup_urls.erase(url.backup_urls.begin());

				play_list->setCurrentIndex(cindex);
			}else{
				// TODO retry

// 				qFatal("no urls left, all urls dead, quit");
 				//play_list->setCurrentIndex(cindex);
			}
		}
	}
}

