#include <QApplication>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QObject>
#include <QDebug>
#include <QMessageBox>
#include <QUrlQuery>
#include <QUrl>
#include <QMap>
#include <QFile>
#include <QTextCodec>

//Инициализация QSql
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>

//Работа с файлами
#include <QDirIterator>
#include <QDir>

#include <windows.h>

#include <QProcess>
#include <QFileDialog>

#include <QDomDocument>
#include <QSettings>

#include <QCommandLineParser>

//Считывает файл в base64
QString ReadBase64FromFile(QString path)
{
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    QString ret = f.readAll().toHex();
    f.close();
    return ret;
}

bool WriteBase64ToFile(QString base64, QString path)
{
    QFile f(path);
    f.open(QIODevice::WriteOnly);

    QByteArray extract;
    extract.append(base64);

    f.write(QByteArray::fromHex(extract));
    f.close();
    return true;
}

//Общедоступный объект, осуществляющий подключение к базе данных
QSqlDatabase db;

//Объект настроек проекта, получающий данные из ini файлы
QSettings* settings;

//Функция для разбора GET параметров у ссылки
QMap<QString,QString> ParseUrlParameters(QString &url)
{
    //Заранее создаем коллекцию для возвращаемого значения
    QMap<QString,QString> ret;

    //Параметров нет. Возвращаем пустую коллекцию
    if(url.indexOf('?')==-1)
    {
        return ret;
    }

    //Обрезаем из url все до вопроса
    QString tmp = url.right(url.length()-url.indexOf('?')-1);

    //Разбиваем в коллекцию параметров через разделитель &
    QStringList paramlist = tmp.split('&');

    //Ввожу параметры в коллекцию
    for(int i=0;i<paramlist.count();i++)
    {
        QStringList paramarg = paramlist.at(i).split('=');
        ret.insert(paramarg.at(0),paramarg.at(1));
    }

    //Вывожу в консоль полученные параметры
    QMapIterator<QString, QString> i(ret);
    while (i.hasNext()) {
        i.next();
        qDebug() << i.key() << ":" << i.value() << endl;
    }

    return ret;
}

//Этот класс содержит список файлов и архивирует файлы. Делает вывод
class Archiver{
private:
    QList<QString> filelist;

public:
    Archiver()
    {
    }

    QList<QString> GetList()
    {
        return filelist;
    }

    bool IsFileInList(QString file)
    {
        for(int i=0;i<filelist.count();i++){
            if(filelist.at(i)==file) return true;
        }
        return false;
    }

    void AddFile(QString file)
    {
        filelist.append(file);
    }

    void RemoveFile(QString file)
    {
        for(int i=0;i<filelist.count();i++)
        {
            if(filelist.at(i)==file){
                filelist.removeAt(i);
                return;
            }
        }
        qDebug() << "Файла не существует";
    }

    bool CreateArchive(QString filename){
        QDomDocument xml;
        QDomElement root=xml.createElement("root");
        xml.appendChild(root);

        //добавление файла в архив
        for(int i =0; i<filelist.count();i++)
        {
            QDomElement node=xml.createElement("item");
            node.setAttribute("file","true");
            node.setAttribute("name",QFileInfo(filelist[i]).fileName());
            node.setAttribute("base64",ReadBase64FromFile(filelist[i]));
            root.appendChild(node);
        }

        //Добавление имени пользователя
        QDomElement name=xml.createElement("item");
        name.setAttribute("info","true");
        name.setAttribute("appname",settings->value("AppName","DEFAULTNAME").toString());
        root.appendChild(name);

        QFile file1(filename);
        if(!file1.open(QIODevice::WriteOnly))
        {
            return false;
        }
        else
        {
            file1.write(qCompress(xml.toByteArray()));
            file1.close();
        }
        return true;
    }

    bool ExtractArchive(QString path)
    {
        QFile f(path);
        f.open(QIODevice::ReadOnly);
        if(!f.isOpen()) return false;

        QDomDocument doc;
        if (!doc.setContent(qUncompress(f.readAll()))) {
            f.close();
            return false;
        }
        f.close();

        //Получить имя пользователя, создавшего архив
        QString name;
        QDomNode root = doc.firstChild();
        for(int i=0;i<root.childNodes().count();i++)
        {
            if(root.childNodes().at(i).attributes().contains("info"))
            {
                name=root.childNodes().at(i).toElement().attribute("appname","-1");
            }
        }

        if(QDir(QFileInfo(path).dir().path()+"/"+name).exists()) return false;
        QFileInfo(path).dir().mkdir(name);
        for(int i=0;i<root.childNodes().count();i++)
        {
            if(root.childNodes().at(i).attributes().contains("file"))
            {
                WriteBase64ToFile(root.childNodes().at(i).toElement().attribute("base64","-1"),QFileInfo(path).dir().path()+"/"+name+"/"+root.childNodes().at(i).toElement().attribute("name","-1"));
            }
        }

        return true;
    }

}archiver;


//Этот класс собирает код html для ответа клиенту по текущей ссылке
class HtmlResponse{

private:
    //Перечисление страниц. Страница должна начинаться с /. Допустимо слитное написание для подстраниц
    QStringList pages;

    //HTML код главной страницы
    const QString mainpage="<table width='100%' height='100%' ><tr><td align='center'>"
                           "<h2>Умный архиватор</h2>"
                           "<a href='/files?dir=/' target='_self'>Работа с файлами</a><br><a href='/admin' target='_self'>Панель администратора</a>"
                           "</td></tr></table>";

    const QString infopage="<h2>{TITLE}</h2><br><p>{INFO}</p><hr><a href='{BACKURL}'>Назад</a>";

public:
    HtmlResponse()
    {
        //Запись страниц
        pages<<"/"<<"/index"<<"/files"<<"/files/add" << "/archiver" << "/archiver/remove" << "/archiver/create" << "/archiver/open";
    }

    QString GetHtml(QString request)
    {
        QMap<QString,QString> params=ParseUrlParameters(request);

        //Параметры были. Для упрощенной обработки их стоит удалить
        if(params.count()!=0)
        {
            request.remove(request.indexOf('?'),request.length()-request.indexOf('?'));
        }

        qDebug() << "Запрос на страницу "<<request;

        QString response;
        switch(pages.indexOf(request))
        {
            //Главная страница. Выводит фрейм
            case 0:
            {
                response = "<html><head><HTA:APPLICATION APPLICATIONNAME='oHTA' scroll='no' ID='oHTA' VERSION='1.0'/></head><body>"
                           "<iframe src=\"http://127.0.0.1:1488/index\" width=\"100%\" height=\"100%\" application=\"yes\"> "
                           "</body></html>";
            }
            break;

            //Фрейм меню выбора страницы
            case 1:
            {
                response = mainpage;
            }
            break;

            //Диспетчер файлов
            case 2:
            {
                QString dir=QUrl::fromPercentEncoding(QByteArray::fromStdString(params.value("dir").toStdString()));
                dir.replace("+"," ");


                response+="<table width='100%' border='1'><tr><td>";

                //Блок списка фалов
                response+="<table border='1' align='center' style='padding: 3px; margin-top: 10px; margin-bottom: 10px;'>";
                response+="<tr><td style='border: 0px solid transparent;'><p style='font-weight: bold;'>Папки</p></td></tr>";
                QDir directory(dir);
                //Исправление пути для корневого каталога
                if(dir=="/") dir="";
                QStringList folders = directory.entryList(QDir::NoDotAndDotDot | QDir::System | QDir::Hidden  |  QDir::AllDirs, QDir::DirsFirst);
                for(int i=0;i<folders.count();i++) {
                    response+="<tr><td>"+folders.at(i)+"</td><td><a href='/files?dir="+dir+"/"+folders.at(i)+"'>Откыть</a></td></tr>";
                }
                response+="<tr><td style='border: 0px solid transparent;'><br><p style='font-weight: bold;'>Файлы</p></td></tr>";
                QStringList files = directory.entryList(QDir::NoDotAndDotDot | QDir::System | QDir::Hidden  |  QDir::Files, QDir::DirsFirst);
                for(int i=0;i<files.count();i++) {
                    //Этот файл- наш архив. Подобные не архивируем, а предлагаем извлеч
                    if(QFileInfo(files[i]).suffix()=="uffarch")
                    {
                        response+="<tr><td>"+files.at(i)+"</td><td><a href='/archiver/open?path="+dir+"/"+files.at(i)+"'>Извлеч</a></td></tr>";
                    }
                    else
                    {
                        QString addurl = (archiver.IsFileInList(dir+"/"+files.at(i)))?"<a href='#'>Добавлено</a>":"<a href='/files/add?file="+dir+"/"+files.at(i)+"'>Добавить к архиву</a>";
                        response+="<tr><td>"+files.at(i)+"</td><td>"+addurl+"</td></tr>";
                    }
                }
                QDir updirectory(directory.path());
                updirectory.cdUp();
                response+="<tr><td style='border: 0px solid transparent;'><a href='/files?dir="+updirectory.path()+"'>Назад</a></td></tr>";
                response+="</table>";

                response+="</td><td style='vertical-align: top;' align='center'><a href='/archiver'><button onclick='window.location.href=\"/archiver\";' style=' margin-top: 10px;'>Очередь к добавлению</button></a></br>";
                if(archiver.GetList().count()>0)response+="<button onclick='window.location.href=\"/archiver/create?path="+directory.path()+"/\""+"+window.prompt(\"Введите имя архива\")+\".uffarch\";' style=' margin-top: 10px;'>Создать архив</button>";
                response+="</td></tr></table>";
            }
            break;

            //Добавление файла в архив
            case 3:
            {
                //Преобразование из "процентного вида <form>" в человеческий вид
                QString file=QUrl::fromPercentEncoding(QByteArray::fromStdString(params.value("file").toStdString()));
                file.replace("+"," ");

                qDebug() << "Добавление файла "+file;

                archiver.AddFile(file);


                QFileInfo fileinfo(file);
                response = infopage;
                response =  response.replace("{TITLE}","Файл добавлен в очередь к архиваци!").replace("{INFO}",file).replace("{BACKURL}","/files?dir="+fileinfo.dir().path());
            }
            break;

            //Список очереди к архивации
            case 4:
            {
                QList<QString> filelist= archiver.GetList();
                response+="<table>";
                response+="<tr><td><p style='font-weight: bold;'>Список файлов в архиве</p></td></tr>";
                for(int i=0;i<filelist.count();i++)
                {
                    response+="<tr><td>"+filelist.at(i)+"</td><td><a href='/archiver/remove?file="+filelist.at(i)+"'>Убрать из списка</a></tr>";
                }
                response+="<tr><td><a href='/files?dir=/'>К выбору файлов</a></td></tr>";
                response+="</table>";
            }
            break;

            //Убрать файл из очереди
            case 5:
            {
                //Преобразование из "процентного вида <form>" в человеческий вид
                QString file=QUrl::fromPercentEncoding(QByteArray::fromStdString(params.value("file").toStdString()));
                file.replace("+"," ");

                qDebug() << "Удаление файла "+file;

                archiver.RemoveFile(file);

                QFileInfo fileinfo(file);
                response = infopage;
                response =  response.replace("{TITLE}","Файл убран из очереди к архиваци!").replace("{INFO}",file).replace("{BACKURL}","/archiver");
            }
            break;

            //Создать архив
            case 6:
            {
                QString path=QUrl::fromPercentEncoding(QByteArray::fromStdString(params.value("path").toStdString()));
                path.replace("+"," ");
                response=infopage;
                if(archiver.CreateArchive(path))response =  response.replace("{TITLE}","Файл создан!").replace("{INFO}","Файл успешно создан").replace("{BACKURL}","/files?dir="+QFileInfo(path).dir().path());
                else response =  response.replace("{TITLE}","Файл не создан!").replace("{INFO}","Не удается создать архив...").replace("{BACKURL}","/files?dir=/");
            }
            break;

            //Извлеч архив
            case 7:
            {
                QString path=QUrl::fromPercentEncoding(QByteArray::fromStdString(params.value("path").toStdString()));
                path.replace("+"," ");
                response=infopage;
                if(archiver.ExtractArchive(path))response =  response.replace("{TITLE}","Архив извлечен!").replace("{INFO}","Файлы успешно созданы").replace("{BACKURL}","/files?dir="+QFileInfo(path).dir().path());
                else response =  response.replace("{TITLE}","Не удается извлеч архив!").replace("{INFO}","Не удается извлечь данные из архива...").replace("{BACKURL}","/files?dir=/");
            }
            break;

            default:
                response = "<h2>404</h2>";
            break;
        }

        return response;
    }
}ResponseManager;

//Этот класс осуществляет низкоуровневую обработку запросов к серверу
class ConnectionManager
{
private:
    QTcpServer *tcpServer;

public:
    ConnectionManager()
    {
        tcpServer = new QTcpServer();

        //Ожидание запроса на подключение
        QObject::connect(tcpServer,QTcpServer::newConnection,[this]()
        {
            qDebug()<<"Новое подключение!";

            //Выделение сокета и передача управления обработчику
            QTcpSocket* tempClientSocket=tcpServer->nextPendingConnection();
            QObject::connect(tempClientSocket,QTcpSocket::readyRead,[tempClientSocket,this](){
                //Считывание запроса
                QString request;


                if(tempClientSocket->bytesAvailable())
                {
                    qDebug() << "Чтение сокета";
                    QString tmp= tempClientSocket->readAll();
                    request=tmp.split(" ").at(1);
                    qDebug() << "Считано: "<<request;
                }
                else
                {
                    qDebug() << "Сокет отброшен: данные не получены!";
                    tempClientSocket->close();
                }



                //Вывод ответа клиенту
                QTextStream os(tempClientSocket);
                os.setAutoDetectUnicode(true);
                os << "HTTP/1.0 200 Ok\r\n";
                os << "Content-Type: text/html; charset=\"utf-8\"\r\n";
                os << "\r\n";
                os << ResponseManager.GetHtml(request);
                os << "\n";
                tempClientSocket->close();
            });
        });

        //Запуск сервера
        if (!tcpServer->listen(QHostAddress::LocalHost, 1488)) {
            qDebug() << "Ошибка: порт занят!";
            QMessageBox msg;
            msg.setText("Сервер не запущен, порт занят!");
            msg.exec();
        } else {
            qDebug() << "Сервер запущен!";

            //Запустить интерпретатор веб приложений Microsoft на весь экран
            Sleep(1000);
            system("cmd /c start /max mshta http://127.0.0.1:1488/");
        }
    }
};

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    settings = new QSettings(QDir::currentPath() + "/my_config_file.ini", QSettings::IniFormat);

    QCommandLineParser parser;
    QCommandLineOption init("i", "Записать стандартные параметры в ini файл");
    parser.addOption(init);
    parser.process(a);

    if(parser.isSet(init))
    {
        qDebug() << "Инициализация файла настроек...";
        settings->setValue("AppName", "DEFAULTNAME");
        settings->sync();
    }

    //Устанавливаю кодировку QString в UTF8
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));

    //Подключаюсь к базе данных
    db = QSqlDatabase::addDatabase(settings->value("DRIVER","QODBC").toString());
    db.setDatabaseName(settings->value("ConnectionString","Driver={Microsoft Access Driver (*.mdb)};DSN='';DBQ=C:\\Projects\\SmartArchiver\\SmartArchiver.mdb").toString());
    if(!db.open())
    {
        QMessageBox msg;
        msg.setText("Не удается подключиться к базе данных!"+db.lastError().text());
        msg.exec();
        return 0;
    };

    ConnectionManager manager;

    //Закрытие базы данных при завершении
    int exec = a.exec();
    db.close();
    return exec;
}
