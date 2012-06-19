#include <QtSql>
#include <QDebug>

#include "treeeditdialog.h"

TreeEditDialog::TreeEditDialog(QWidget *parent, QSqlDatabase *db) :
  QDialog(parent)
{
  db_ = db;
  model_ = new TreeModel();

  createFolder_ = new QAction(tr("createFolder"), this);
  deleteNode_ = new QAction(tr("deleteNode"), this);
  moveUp_ = new QAction(tr("moveUp"), this);
  moveDown_ = new QAction(tr("moveDown"), this);
  moveLeft_ = new QAction(tr("moveLeft"), this);
  moveRight_ = new QAction(tr("moveRight"), this);

  toolBar_ = new QToolBar();
  toolBar_->addAction(createFolder_);
  toolBar_->addSeparator();
  toolBar_->addAction(deleteNode_);
  toolBar_->addSeparator();
  toolBar_->addAction(moveUp_);
  toolBar_->addAction(moveDown_);
  toolBar_->addAction(moveLeft_);
  toolBar_->addAction(moveRight_);

  view_ = new QTreeView();
  view_->setModel(model_);
  view_->header()->hide();
  for (int i = 1; i < model_->columnCount(); ++i)
    view_->header()->hideSection(i);
  view_->expandAll();

  buttonBox_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

  QVBoxLayout *mainLayout = new QVBoxLayout();
  mainLayout->setMargin(1);
  mainLayout->addWidget(toolBar_);
  mainLayout->addWidget(view_);
  mainLayout->addWidget(buttonBox_);
  setLayout(mainLayout);

  connect(view_, SIGNAL(clicked(QModelIndex)), this, SLOT(slotUpdateActions(QModelIndex&)));
  connect(buttonBox_, SIGNAL(accepted()), this, SLOT(accept()));
  connect(buttonBox_, SIGNAL(rejected()), this, SLOT(reject()));
  connect(createFolder_, SIGNAL(triggered()), this, SLOT(slotCreateFolder()));
  connect(deleteNode_, SIGNAL(triggered()), this, SLOT(slotDeleteNode()));
  connect(moveUp_, SIGNAL(triggered()), this, SLOT(slotMoveUp()));
  connect(moveDown_, SIGNAL(triggered()), this, SLOT(slotMoveDown()));
  connect(moveLeft_, SIGNAL(triggered()), this, SLOT(slotMoveLeft()));
  connect(moveRight_, SIGNAL(triggered()), this, SLOT(slotMoveRight()));
}

void TreeEditDialog::renewModel(void)
{
  QItemSelectionModel *oldModel = view_->selectionModel();
  model_ = new TreeModel();
  view_->setModel(model_);
  view_->expandAll();
  delete oldModel;
}

void TreeEditDialog::slotUpdateActions(QModelIndex &index)
{
  if (index.isValid()) {
    deleteNode_->setEnabled(true);
    moveUp_->setEnabled(true);
    moveDown_->setEnabled(true);
    moveLeft_->setEnabled(true);
    moveRight_->setEnabled(true);
  } else {
    deleteNode_->setEnabled(false);
    moveUp_->setEnabled(false);
    moveDown_->setEnabled(false);
    moveLeft_->setEnabled(false);
    moveRight_->setEnabled(false);
  }
}

void TreeEditDialog::slotCreateFolder()
{
  // поиск количества потомков в корне
  QSqlQuery q(*db_);
  q.exec("SELECT max(rowToParent) FROM feeds WHERE parentId=0");
  qDebug() << __FUNCTION__ << q.lastQuery();
  qDebug() << __FUNCTION__ << q.boundValues();
  int newRowToParent = 0;
  if (q.next()) newRowToParent = q.value(0).toInt() + 1;
  qDebug() << __FUNCTION__ << "newRowToParent =" << newRowToParent;

  // вставляем новую папку
  q.prepare("INSERT INTO feeds(hasChildren, parentId, rowToParent, text) "
         "VALUES(0, 0, :rowToParent, 'New folder')");
  q.bindValue(":rowToParent", newRowToParent);
  q.exec();

  renewModel();
}

void TreeEditDialog::slotDeleteNode()
{
  QModelIndex index = view_->currentIndex();
  if (!index.isValid()) return;
  if (0 < model_->rowCount(index)) return;

  // удаляем узел
  QSqlQuery q(*db_);
  q.prepare("DELETE FROM feeds WHERE id=:id");
  q.bindValue(":id", index.data(Qt::UserRole));
  q.exec();
  qDebug() << __FUNCTION__ << q.lastQuery();
  qDebug() << __FUNCTION__ << q.boundValues();

  // перекомпоновка оставшихся лент (чтобы не осталось "дырки" в нумерации)
  q.prepare("SELECT id, rowToParent FROM feeds WHERE parentId=:parentId");
  q.bindValue(":parentId", index.parent().data(Qt::UserRole));
  q.exec();
  qDebug() << __FUNCTION__ << q.lastQuery();
  qDebug() << __FUNCTION__ << q.boundValues();
  bool hasChildren = false;  //! флаг того, что у старого родителя ещё остались дети
  while (q.next()) {
    hasChildren = true;
    int id = q.value(0).toInt();
    int rowToParent = q.value(1).toInt();
    if (index.row() < rowToParent) {
      QSqlQuery q2(*db_);
      q2.prepare("UPDATE feeds SET rowToParent=:reoToParent WHERE id=:id");
      q2.bindValue(":rowToParent", rowToParent-1);
      q2.bindValue(":id", id);
      q2.exec();
      qDebug() << __FUNCTION__ << q2.lastQuery() << q.lastError();
      qDebug() << __FUNCTION__ << q2.boundValues();
    }
  }
  // если детей не осталось, то помечаем, что у родителя не осталось детей
  if (!hasChildren) {
    q.prepare("UPDATE feeds SET hasChildren=0 WHERE id=:id");
    q.bindValue(":id", index.parent().data(Qt::UserRole));
    q.exec();
    qDebug() << __FUNCTION__ << q.lastQuery();
    qDebug() << __FUNCTION__ << q.boundValues();
  }

  renewModel();
}

void TreeEditDialog::slotMoveUp()
{
  QModelIndex index = view_->currentIndex();
  if (!index.isValid()) return;
  if (index.row() <= 0) return;

  // меняем местами с предыдущим рядом
  QSqlQuery q(*db_);
  q.prepare("UPDATE feeds SET rowToParent=:rowToParent WHERE id=:id");
  q.bindValue(":rowToParent", index.row()-1);
  q.bindValue(":id", index.data(Qt::UserRole));
  q.exec();
  q.prepare("UPDATE feeds SET rowToParent=:rowToParent WHERE id=:id");
  q.bindValue(":rowToParent", index.row());
  q.bindValue(":id", model_->index(
      index.row()-1, index.column(), index.parent()).data(Qt::UserRole));
  q.exec();

  renewModel();
}

void TreeEditDialog::slotMoveDown()
{
  QModelIndex index = view_->currentIndex();
  if (!index.isValid()) return;
  if (model_->rowCount(index.parent()) <= index.row()+1) return;

  // меняем местами со следующим рядом
  QSqlQuery q(*db_);
  q.prepare("UPDATE feeds SET rowToParent=:rowToParent WHERE id=:id");
  q.bindValue(":rowToParent", index.row()+1);
  q.bindValue(":id", index.data(Qt::UserRole));
  q.exec();
  q.prepare("UPDATE feeds SET rowToParent=:rowToParent WHERE id=:id");
  q.bindValue(":rowToParent", index.row());
  q.bindValue(":id", model_->index(
      index.row()+1, index.column(), index.parent()).data(Qt::UserRole));
  q.exec();

  renewModel();
}

void TreeEditDialog::slotMoveLeft()
{
  QModelIndex index = view_->currentIndex();
  if (!index.isValid()) return;
  if (!index.parent().isValid()) return;

  // поиск количества потомков у нового родителя
  qDebug() << __FUNCTION__ << index.data();
  qDebug() << __FUNCTION__ << index.data(Qt::UserRole);
  qDebug() << __FUNCTION__ << index.parent().data(Qt::UserRole);
  qDebug() << __FUNCTION__ << index.parent().parent().data(Qt::UserRole);
  QSqlQuery q(*db_);
  q.prepare("SELECT max(rowToParent) FROM feeds WHERE parentId=:parentId");
  if (index.parent().parent().isValid())
    q.bindValue(":parentId", index.parent().parent().data(Qt::UserRole));
  else
    q.bindValue(":parentId", 0);
  q.exec();
  qDebug() << __FUNCTION__ << q.lastQuery();
  qDebug() << __FUNCTION__ << q.boundValues();
  int newRowToParent = 0;
  if (q.next()) newRowToParent = q.value(0).toInt() + 1;
  qDebug() << __FUNCTION__ << "newRowToParent =" << newRowToParent;

  // перемещение ленты к новому родителю
  q.prepare("UPDATE feeds SET parentId=:parentId, rowToParent=:newRowToParent "
            "WHERE id=:id");
  if (index.parent().parent().isValid())
    q.bindValue(":parentId", index.parent().parent().data(Qt::UserRole));
  else
    q.bindValue(":parentId", 0);
  q.bindValue(":newRowToParent", newRowToParent);
  q.bindValue(":id", index.data(Qt::UserRole));
  q.exec();
  qDebug() << __FUNCTION__ << q.lastQuery();
  qDebug() << __FUNCTION__ << q.boundValues();

  // перекомпоновка оставшихся лент (чтобы не осталось "дырки" в нумерации)
  q.prepare("SELECT id, rowToParent FROM feeds WHERE parentId=:parentId");
  q.bindValue(":parentId", index.parent().data(Qt::UserRole));
  q.exec();
  qDebug() << __FUNCTION__ << q.lastQuery();
  qDebug() << __FUNCTION__ << q.boundValues();
  bool hasChildren = false;  //! флаг того, что у старого родителя ещё остались дети
  while (q.next()) {
    hasChildren = true;
    int id = q.value(0).toInt();
    int rowToParent = q.value(1).toInt();
    if (index.row() < rowToParent) {
      QSqlQuery q2(*db_);
      q2.prepare("UPDATE feeds SET rowToParent=:reoToParent WHERE id=:id");
      q2.bindValue(":rowToParent", rowToParent-1);
      q2.bindValue(":id", id);
      q2.exec();
      qDebug() << __FUNCTION__ << q2.lastQuery() << q.lastError();
      qDebug() << __FUNCTION__ << q2.boundValues();
    }
  }
  // если детей не осталось, то помечаем, что у родителя не осталось детей
  if (!hasChildren) {
    q.prepare("UPDATE feeds SET hasChildren=0 WHERE id=:id");
    q.bindValue(":id", index.parent().data(Qt::UserRole));
    q.exec();
    qDebug() << __FUNCTION__ << q.lastQuery();
    qDebug() << __FUNCTION__ << q.boundValues();
  }

  renewModel();
}

void TreeEditDialog::slotMoveRight()
{
  QModelIndex index = view_->currentIndex();
  if (!index.isValid()) return;
  if (index.row() <= 0) return;

  // поиск количества потомков у нового родителя
  qDebug() << __FUNCTION__ << index.data();
  qDebug() << __FUNCTION__ << index.data(Qt::UserRole);
  qDebug() << __FUNCTION__ << index.parent().data(Qt::UserRole);
  qDebug() << __FUNCTION__ << index.parent().parent().data(Qt::UserRole);
  QSqlQuery q(*db_);
  q.prepare("SELECT max(rowToParent) FROM feeds WHERE parentId=:parentId");
  q.bindValue(":parentId", model_->index(
      index.row()-1, index.column(), index.parent()).data(Qt::UserRole));
  q.exec();
  qDebug() << __FUNCTION__ << q.lastQuery();
  qDebug() << __FUNCTION__ << q.boundValues();
  int newRowToParent = 0;
  if (q.next()) {
    qDebug() << q.value(0);
    if (!q.value(0).isNull())
      newRowToParent = q.value(0).toInt() + 1;
  }
  qDebug() << __FUNCTION__ << "newRowToParent =" << newRowToParent;

  // перекомпоновка остающихся лент (чтобы не осталось "дырки" в нумерации)
  q.prepare("SELECT id, rowToParent FROM feeds WHERE parentId=:parentId");
  if (index.parent().isValid())
    q.bindValue(":parentId", index.parent().data(Qt::UserRole));
  else
    q.bindValue(":parentId", 0);
  q.exec();
  qDebug() << __FUNCTION__ << q.lastQuery();
  qDebug() << __FUNCTION__ << q.boundValues();
  while (q.next()) {
    int id = q.value(0).toInt();
    int rowToParent = q.value(1).toInt();
    if (index.row() < rowToParent) {
      QSqlQuery q2(*db_);
      q2.prepare("UPDATE feeds SET rowToParent=:rewToParent WHERE id=:id");
      q2.bindValue(":rowToParent", rowToParent-1);
      q2.bindValue(":id", id);
      q2.exec();
      qDebug() << __FUNCTION__ << q2.lastQuery() << q.lastError();
      qDebug() << __FUNCTION__ << q2.boundValues();
    }
  }

  // перемещение ленты к новому родителю
  q.prepare("UPDATE feeds SET parentId=:parentId, rowToParent=:newRowToParent "
            "WHERE id=:id");
  q.bindValue(":parentId", model_->index(
      index.row()-1, index.column(), index.parent()).data(Qt::UserRole));
  q.bindValue(":newRowToParent", newRowToParent);
  q.bindValue(":id", index.data(Qt::UserRole));
  q.exec();
  qDebug() << __FUNCTION__ << q.lastQuery();
  qDebug() << __FUNCTION__ << q.boundValues();

  // помечаем, что у нового родителя теперь есть дети
  // (на всякий случай, если у него их ещё не было)
  q.prepare("UPDATE feeds SET hasChildren=1 WHERE id=:id");
  q.bindValue(":id", model_->index(
      index.row()-1, index.column(), index.parent()).data(Qt::UserRole));
  q.exec();
  qDebug() << __FUNCTION__ << q.lastQuery();
  qDebug() << __FUNCTION__ << q.boundValues();

  renewModel();
}
