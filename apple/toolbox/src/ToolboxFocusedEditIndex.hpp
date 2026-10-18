#ifndef LOKA_TOOLBOX_FOCUSED_EDIT_INDEX_HPP
#define LOKA_TOOLBOX_FOCUSED_EDIT_INDEX_HPP

#include <cstddef>
#include <vector>

/** Tracks the focused edit binding by position so std::vector storage moves do
    not leave a long-lived pointer behind. Erase transitions are kept here so
    the binding list and its focus identity cannot drift independently. */
class ToolboxFocusedEditIndex
{
public:
  ToolboxFocusedEditIndex()
      : index_(noIndex())
  {
  }

  void focus(std::size_t index)
  {
    index_ = index;
  }

  void clear()
  {
    index_ = noIndex();
  }

  void erase(std::size_t erasedIndex)
  {
    if (index_ == noIndex())
    {
      return;
    }
    if (index_ == erasedIndex)
    {
      clear();
    }
    else if (erasedIndex < index_)
    {
      --index_;
    }
  }

  template <typename T>
  T *resolve(std::vector<T> &bindings) const
  {
    return index_ < bindings.size() ? &bindings[index_] : 0;
  }

  /** Report the focused position while it names one of `size` rows; leave out untouched otherwise. */
  bool query(std::size_t size, std::size_t &out) const
  {
    if (index_ >= size)
      return false;
    out = index_;
    return true;
  }

private:
  static std::size_t noIndex()
  {
    return static_cast<std::size_t>(-1);
  }

  std::size_t index_;
};

#endif // LOKA_TOOLBOX_FOCUSED_EDIT_INDEX_HPP
