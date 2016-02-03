#include <set>
#include <map>

#include <QCoreApplication>
#include <QtCore>
#include <QtXml>
#include <QRegExp>

#define PRO_FILE_EXT ".pro"
#define SOLUTION_EXT ".sln"
#define PROJECT_EXT ".vcxproj"

QByteArray readAll(const QString& fileName) {
    QByteArray result;
    QFile file(fileName);
    if( file.open(QFile::ReadOnly) ) {
        result = file.readAll();
        if( result.isEmpty() ) {
            qWarning() << "file" << fileName <<  "is empty";
        } else {
            qDebug() << "read file " << fileName;
        }
    } else {
        qWarning() << "error: can not open file" << fileName;
    }

    return std::move(result);
}

void writeAll(const QString& fileName, QByteArray&& data) {
    if( !data.isEmpty() ) {
        QFile file(fileName);
        if( file.open(QFile::WriteOnly) ) {
            auto sz = data.size();
            auto wrsz = file.write( std::move(data) );
            if( sz == wrsz ) {
                qDebug() << "success write file " << fileName;
            } else {
                qWarning() << "error QFile::write, file =" << fileName;
            }
        } else {
            qWarning() << "can not open file " << fileName;
        }
    } else {
        qWarning() << "write empty data to file " << fileName;
    }
}

QByteArrayList parseSolutionFile(const QByteArray& slnText) {
    qDebug() << "prasing solution file";

    QByteArrayList projs;

    int pos = 0;
    auto findProject = [&slnText, &pos, &projs] () {
        const char rtag[] =  PROJECT_EXT "\"";
        const char ltagsym = '\"';
        enum{ rtaglen = sizeof(rtag)-1 };

        int next = slnText.indexOf(rtag, pos);
        if( next > 0 ) {
            int prev = slnText.lastIndexOf(ltagsym, next);
            if( prev > 0 ) {
                ++prev;
                pos = next + rtaglen;
                projs.push_back( slnText.mid(prev, pos - prev - 1) );
                return true;
            }
        }

        return false;
    };

    while(findProject()) {}

    return projs;
}


void genSolutionProFile(const QString& sln, const QByteArrayList& projs) {
    qDebug() << "generate solution pro file";

    QByteArray mem;
    mem += "TEMPLATE = subdirs\n";
    mem += "SUBDIRS +=";
    for(auto proj : projs ) {
        mem += ' ';
        mem += proj.replace('\\', '/').replace(PROJECT_EXT, PRO_FILE_EXT);
    }
    mem += '\n';

    writeAll(QString(sln).replace(SOLUTION_EXT, PRO_FILE_EXT), std::move(mem));
}

typedef QMap<QString, QString> envmap_t;

envmap_t enviroment() {
    qDebug() << "read enviroment";

    QMap<QString, QString> env;
    auto sysenv = QProcessEnvironment::systemEnvironment();
    for(auto& key : sysenv.keys()) {
        env[key] = sysenv.value(key);
    }

    return std::move(env);
}

QString replaceVar(const QString& target, const envmap_t& env, int* replaceCount = nullptr) {
   const QString envSyms("%$");
   auto isEnvSym = [&envSyms] (const QChar& ch) {
       return envSyms.contains(ch);
   };
   auto isNameSym = [] (const QChar& ch) {
       return !ch.isLetter() && !ch.isDigit() && ch != '_';
   };
   auto mid = [&target] (const QString::const_iterator& begin, const QString::const_iterator& end) {
       return target.mid(begin - std::begin(target),  end - begin);
   };

   int tmp;
   if(!replaceCount) {
       replaceCount = &tmp;
   }

   QString res;
   auto pos = std::begin(target);
   auto copy_pos = std::begin(target);
   while(pos != std::end(target)) {
       auto start = std::find_if(pos, std::end(target), isEnvSym);

       if( start != std::end(target) ) {
           auto end = start;
           ++end;

           bool brsym = (end != std::end(target) && *end == '(');
           if( brsym ) {
               ++end;
           }

           auto tmp = std::find_if(end, std::end(target), isNameSym);

           if( end != tmp ) {
               auto name = mid(end, tmp);
               end = tmp;

               if( brsym && end != std::end(target) && *end == ')' ) {
                   ++end;
               }

               if(*start == '%' && end != std::end(target) && *end == '%') {
                   ++end;
               }

               auto fnd = env.find( name );
               if( fnd != std::end(env) ) {
                   res += mid(copy_pos, start);
                   res += *fnd;
                   copy_pos = end;
                   ++( *(replaceCount) );
               }
           }
           start = end;
       }

       pos = start;
   }

   return (res.isEmpty()) ? target : 
	   std::move( res += target.right(std::end(target) - copy_pos) );
}

class Enviroment {
private:
    QString cd_;
    envmap_t env_;
    QString fileName_;
    QByteArray data_;

public:
    Enviroment(const QByteArray& fileName, const QString& prefix, Enviroment* parent = 0)
        : cd_(QDir::currentPath()),
          env_((parent) ? parent->env_ : enviroment()),
          fileName_(),
          data_()
    {
        QFileInfo info(fileName);
		env_[prefix + "Dir"] = info.absolutePath();
		env_[prefix + "Ext"] = info.suffix();
		env_[prefix + "FileName"] = info.fileName();
		env_[prefix + "Name"] = info.baseName();
		env_[prefix + "Path"] = info.absoluteFilePath();
		env_["PWD"] = info.absolutePath();

        if(QDir::setCurrent(info.path())) {
            qDebug() << "cd " << QDir::currentPath();
            auto tempFileName = info.fileName();
            auto temp =  readAll(tempFileName);
            if(!temp.isEmpty()) {
                fileName_ = tempFileName;
                data_ = std::move(temp);
            }
        } else {
            qWarning() << "invalid cd : " << info.path();
        }
    }

    ~Enviroment() {
        if(!cd_.isEmpty()) {
            QDir::setCurrent(cd_);
        }
    }

    void setEnv(const QString& name, const QString& value) {
        env_[name] = value;
    }

    void addEnv(const QString& name, const QString& value) {
        env_.insert(name, value);
    }

	QString value(const QString& key, const QString& defValue = QString()) {
		return env_.value(key, defValue);
	}

    operator bool () const { return !data_.isEmpty(); }

    const QByteArray& data() const { return data_; }

    const QString& fileName() const { return fileName_; }

    QString replaceVar(const QString& target, int* replaceCount = 0) const {
        return ::replaceVar(target, env_, replaceCount);
    }
};

QDomElement xml(const QByteArray& data) {
    QDomElement elem;
    QDomDocument doc;
    QString err; int ln, column;
    if( doc.setContent(data, &err, &ln, &column) ) {
        elem = doc.documentElement();
    } else {
        qWarning() << "error parsing xml";
        qWarning() << err << " Line: " << ln << " Column: " << column;
    }

    return elem;
}

typedef std::set<QString> set_t;

set_t envTagParse(Enviroment& env, const QDomElement& elem, const QString& tagName) {
    env.addEnv(tagName, "");

    auto nl = elem.elementsByTagName(tagName);
	set_t items;
    for(int i = 0; i < nl.size(); ++i ) {
        for(auto& value : env.replaceVar(nl.item(i).toElement().text()).split(';', QString::SkipEmptyParts)) {
            items.emplace( std::move(value) );
        }
    }

    QString value;
    for(auto& item : items) {
        if( !value.isEmpty() ) {
            value += ';';
        }
        value += item;
    }

	env.setEnv(tagName, value);

    return items;
}

set_t projectFiles(const QDomElement& elem) {
	set_t files;
    for(auto node : {"ClCompile","CustomBuild","Node"} ) {
        auto nl = elem.elementsByTagName(node);
        for(int i = 0; i < nl.size(); ++i ) {
            auto elem = nl.item(i).toElement();
            if( !elem.isNull() ) {
				auto file = elem.attribute("Include");
				if (!file.isEmpty()) {
					files.emplace(std::move(file));
				}
            }
        }
    }
    return files;
}

template<class SetType, class PredType>
SetType convert(const SetType& set, PredType pred) {
	SetType newSet;
	for (auto value : set ) {
		if (pred(value)) {
			newSet.emplace(std::move(value));
		}
	}
	return newSet;
}

void removeQtCreatorPrepocessorDefinitions(set_t& set) {
	QRegExp re("QT_*_LIB", Qt::CaseSensitive, QRegExp::Wildcard);
	set = convert(set, [&re](const QString& def) { return !re.exactMatch(def); });
	
	for (auto& def : {
		"DEBUG", "_DEBUG", "NDEBUG",
		"WIN32", "WIN64",
		"QT_DLL", "QT_NO_DEBUG",
		"UNICODE", "_UNICODE"
	}) {
		set.erase(def);
	}
}

static const char unixDirSeparator = '/';

void setUnixDirSeparator(QString& sep) {
	auto currentSep = QDir::separator();
	if (currentSep != unixDirSeparator) {
		sep.replace(currentSep, unixDirSeparator);
	}
}

bool isAbsolutePath(const QString& path) {
	return path.size() > 1 && path[0].isLetter() && path[1] == ':';
}

bool replaceFront(QString& path, const QString& old_, const QString& new_) {
	bool ok = path.indexOf(old_, Qt::CaseInsensitive) == 0;
	if (ok) {
		path.replace(0, old_.length(), new_);
	}
	return ok;
}

void convertToProPath(set_t& paths, Enviroment& env) {
	auto qtdir = env.value("QTDIR");

	paths = convert(paths,
		[&qtdir](QString& path) {
			bool ok = (qtdir.isEmpty() || !path.contains(qtdir))
				&& !path.contains("GeneratedFiles");

			if (isAbsolutePath(path)) {
				auto cd = QDir::current();
				auto abs = cd.absolutePath();
				QString rel = ".";
				ok = replaceFront(path, abs, rel);
				if (!ok) {
					if (cd.cdUp()) {
						abs = cd.absolutePath();
						rel += "/..";
						ok = replaceFront(path, abs, rel);
					}
				}
			}

			if (ok) {
				setUnixDirSeparator(path);
				if (!path.isEmpty()) {
					const QString pwdTag("$$PWD");
					const QString pwdTagSep(pwdTag + unixDirSeparator);
					if (path[0] == '.') {
						if (path.size() > 1) {
							if (path[1] == '.') {
								path = pwdTagSep + path;
							}
							else {
								path.remove(0, 1);
								path = pwdTag + path;
							}
						}
					}
					else if (path[0] != unixDirSeparator) {
						path = pwdTagSep + path;
					}
					
					path.replace("/./", "/" + pwdTagSep);
				}
			}

			return ok;
		}
	);
}

void genProFileTag(QByteArray& mem, const QString& tagName, const set_t& set) {
	if (!set.empty()) {
		mem += tagName.toLocal8Bit();
		mem += " +=";
		for (auto& value : set) {
			mem += ' ';
			mem += value.toLocal8Bit();
		}
		mem += '\n';
	}
}

bool isEndDirSeparator(const QString& s) {
	return !s.isEmpty() && (
					s[s.size()-1] == '/' ||
					s[s.size()-1] == '\\'
				);
}

void deleteEndDirSeparator(QString& s) {
	if (s.size() > 1 && isEndDirSeparator(s)) {
		s.chop(1);
	}
}

void addEndDirSeparator(QString& s) {
	if (isEndDirSeparator(s)) {
		s.chop(1);
	}
	s.push_back(unixDirSeparator);
}

void convertToQMakeLib(set_t& libs) {
	libs = convert(libs,
		[](QString& s) {
			const QString suffix(".lib");
			if (s.size() > suffix.size()) {
				auto end = s.size() - suffix.size();
				if (s.lastIndexOf(suffix) == end) {
					s.chop(suffix.size());
					s = 'l' + s;
					return true;
				}
			}
			return false;
		}
	);
}

void convertToQMakeLibPath(set_t& libpaths) {
	libpaths = convert(libpaths,
		[](QString& s) {
			addEndDirSeparator(s);
			s = "-L" + s;
			return true;
		}
	);
}

set_t qtModulesSeparation(set_t& libpaths) {
	set_t modules;

	libpaths.erase("qtmain.lib");
	libpaths.erase("qtmaind.lib");

	libpaths = convert(libpaths,
		[&modules](QString& s) {
			const QString modulePrefix("Qt5");
			const QString modulePostfix(".lib");
			if (s.size() > (modulePrefix.size() + modulePostfix.size())) {
				auto iprfx = s.indexOf(modulePrefix);
				if (iprfx >= 0) {
					iprfx += modulePrefix.size();
					auto end = s.size() - modulePostfix.size();
					if (s.lastIndexOf(modulePostfix) == end) {
						modules.emplace(s.mid(iprfx, end-iprfx).toLower() );
					}
					return false;
				}
			}
			return true;
		}
	);

	modules = convert(modules, 
		[&modules](QString& s) {
			if (s.size() > 1) {
				auto d_pos = s.length() - 1;
				if (s[d_pos] == 'd') {
					if (modules.find(s + 'd') == std::end(modules)) {
						s.chop(1);
					}
				}
			}
			return true;
		}
	);

	return modules;
}

void genProFile(const QString& proj, const QDomElement& elem, Enviroment& env)
{
	qDebug() << "parsing project file";
	auto outdirs = envTagParse(env, elem, "OutDir");
	auto defines = envTagParse(env, elem, "PreprocessorDefinitions");
	auto libpaths = envTagParse(env, elem, "AdditionalLibraryDirectories");
	auto include = envTagParse(env, elem, "AdditionalIncludeDirectories");
	auto dependencies = envTagParse(env, elem, "AdditionalDependencies");
	auto projectFiles = ::projectFiles(elem);
	auto modules = qtModulesSeparation(dependencies);

	removeQtCreatorPrepocessorDefinitions(defines);
	convertToProPath(outdirs, env);
	convertToProPath(libpaths, env);
	convertToProPath(include, env);
	convertToProPath(projectFiles, env);
	convertToQMakeLib(dependencies);
	convertToQMakeLibPath(libpaths);

	qDebug() << "generate project pro file";
	QByteArray mem(
		"TEMPLATE = app\n"
		"macx {\n"
		"\tCONFIG += c++11\n"
		"\tDEFINES += HAVE_PTHREAD\n"
		"\tQMAKE_CFLAGS += -gdwarf-2\n"
		"\tQMAKE_CXXFLAGS += -gdwarf-2\n"
		"\tQMAKE_INFO_PLIST= $${PWD}/Info.plist\n"
		"\tICON = $${PWD}/icon.icns\n"
		"\tdebug { DEFINES += _DEBUG }\n"
		"}\n"
	);

	if (!outdirs.empty()) {
		mem += "DESTDIR = ";
		mem += *outdirs.begin();
		mem += '\n';
	}

	genProFileTag(mem, "QT", modules);
	genProFileTag(mem, "DEFINES", defines);
	genProFileTag(mem, "INCLUDEPATH", include);
	genProFileTag(mem, "LIBS", libpaths);

	auto lpq = dependencies.find("llibpq");
	if (lpq != std::end(dependencies)) {
		mem += "macx { LIBS += -lpq } else { LIBS += -llibpq }\n";
		dependencies.erase(lpq);
	}

	genProFileTag(mem, "LIBS", dependencies);

    struct group {
        QByteArray name;
        std::vector<QString> suffixes;
        QByteArray data;
    } groups[] = {
        { "HEADERS", {"h"}, {}},
        { "SOURCES", {"cpp", "c", "cc"}, {}},
        { "FORMS", {"ui"}, {}},
        { "TRANSLATIONS", {"ts"}, {}},
        { "RESOURCES", {"qrc"}, {}}
    };

    std::map<QString, group*> suffixToGroup;
    for(auto& group : groups ) {
        for(auto& suffix : group.suffixes) {
            suffixToGroup[suffix] = &group;
        }
    }

    for(auto file : projectFiles ) {
        auto suffix = QFileInfo(file).suffix();
        auto it = suffixToGroup.find(suffix);
        if( it != suffixToGroup.end() ) {
            it->second->data += (' ' + file);
        }
    }

    for(auto& group : groups ) {
       if(!group.data.isEmpty()) {
           mem += (group.name + " +=" + group.data + '\n');
       }
    }

    writeAll(QString(proj).replace(PROJECT_EXT, PRO_FILE_EXT), std::move(mem));
}

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    qDebug() << "start " << QDir::currentPath();

    auto slnFileName = a.arguments().value(1).toLocal8Bit();
    if(slnFileName.isEmpty()) {
        qCritical() << "invalid first argument";
        return 1;
    }

    Enviroment sln(slnFileName, "Solution");
    if(sln) {
        auto projs = parseSolutionFile( sln.data() );
        genSolutionProFile(sln.fileName(), projs);
        for(auto proj : projs ) {
            Enviroment pro(proj, "Project", &sln);
            if(pro) {
                genProFile(pro.fileName(), xml(pro.data()), pro );
            }
        }
    }

    qDebug() << "success";
}
