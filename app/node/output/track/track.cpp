/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2019 Olive Team

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#include "track.h"

#include <QDebug>

#include "node/block/gap/gap.h"
#include "node/graph.h"

TrackOutput::TrackOutput() :
  current_block_(this)
{
  track_input_ = new NodeInput("track_in");
  track_input_->add_data_input(NodeParam::kTrack);
  AddParameter(track_input_);

  track_output_ = new NodeOutput("track_out");
  track_output_->set_data_type(NodeParam::kTrack);
  AddParameter(track_output_);
}

Block::Type TrackOutput::type()
{
  return kEnd;
}

Block *TrackOutput::copy()
{
  return new TrackOutput();
}

QString TrackOutput::Name()
{
  return tr("Track");
}

QString TrackOutput::id()
{
  return "org.olivevideoeditor.Olive.track";
}

QString TrackOutput::Category()
{
  return tr("Output");
}

QString TrackOutput::Description()
{
  return tr("Node for representing and processing a single array of Blocks sorted by time. Also represents the end of "
            "a Sequence.");
}

void TrackOutput::set_length(const rational &)
{
  // Prevent length changing on this Block
}

void TrackOutput::Refresh()
{
  QVector<Block*> detect_attached_blocks;

  Block* previous = attached_block();
  while (previous != nullptr) {
    detect_attached_blocks.prepend(previous);

    if (!block_cache_.contains(previous)) {
      emit BlockAdded(previous);
    }

    previous = previous->previous();
  }

  foreach (Block* b, block_cache_) {
    if (!detect_attached_blocks.contains(b)) {
      emit BlockRemoved(b);
    }
  }

  block_cache_ = detect_attached_blocks;

  Block::Refresh();
}

void TrackOutput::GenerateBlockWidgets()
{
  foreach (Block* block, block_cache_) {
    emit BlockAdded(block);
  }
}

TrackOutput *TrackOutput::next_track()
{
  return ValueToPtr<TrackOutput>(track_input_->get_value(0));
}

NodeOutput* TrackOutput::track_output()
{
  return track_output_;
}

void TrackOutput::Process(const rational &time)
{
  // Run default node processing
  Block::Process(time);

  // Set track output correctly
  track_output_->set_value(PtrToValue(this));

  // This node representso the end of the timeline, so being beyond its in point is considered the end of the sequence
  if (time >= in()) {
    texture_output()->set_value(0);
    current_block_ = this;
    return;
  }

  // If we're here, we need to find the current clip to display
  // attached_block() is guaranteed to not be nullptr if we didn't return before
  current_block_ = attached_block();

  // If the time requested is an earlier Block, traverse earlier until we find it
  while (time < current_block_->in()) {
    current_block_ = current_block_->previous();
  }

  // If the time requested is in a later Block, traverse later
  while (time >= current_block_->out()) {
    current_block_ = current_block_->next();
  }

  // At this point, we must have found the correct block so we use its texture output to produce the image
  texture_output()->set_value(current_block_->texture_output()->get_value(time));
}

void TrackOutput::InsertBlockBetweenBlocks(Block *block, Block *before, Block *after)
{
  AddBlockToGraph(block);

  Block::DisconnectBlocks(before, after);
  Block::ConnectBlocks(before, block);
  Block::ConnectBlocks(block, after);
}

void TrackOutput::InsertBlockAfter(Block *block, Block *before)
{
  InsertBlockBetweenBlocks(block, before, before->next());
}

Block *TrackOutput::attached_block()
{
  return ValueToPtr<Block>(previous_input()->get_value(0));
}

void TrackOutput::PrependBlock(Block *block)
{
  AddBlockToGraph(block);

  if (block_cache_.isEmpty()) {
    ConnectBlockInternal(block);
  } else {
    Block::ConnectBlocks(block, block_cache_.first());
  }
}

void TrackOutput::InsertBlockAtIndex(Block *block, int index)
{
  AddBlockToGraph(block);

  if (block_cache_.isEmpty()) {

    // If there are no blocks connected, the index doesn't matter. Just connect it.
    ConnectBlockInternal(block);

  } else if (index == 0) {

    // If the index is 0, it goes at the very beginning
    PrependBlock(block);

  } else if (index >= block_cache_.size()) {

    // Append Block at the end
    AppendBlock(block);

  } else {

    // Insert Block just before the Block currently at that index so that it becomes the new Block at that index
    InsertBlockBetweenBlocks(block, block_cache_.at(index - 1), block_cache_.at(index));

  }
}

void TrackOutput::AppendBlock(Block *block)
{
  AddBlockToGraph(block);

  if (block_cache_.isEmpty()) {
    ConnectBlockInternal(block);
  } else {
    InsertBlockBetweenBlocks(block, block_cache_.last(), this);
  }
}

void TrackOutput::ConnectBlockInternal(Block *block)
{
  AddBlockToGraph(block);

  Block::ConnectBlocks(block, this);
}

void TrackOutput::AddBlockToGraph(Block *block)
{
  // Find the parent graph
  NodeGraph* graph = static_cast<NodeGraph*>(parent());
  graph->AddNodeWithDependencies(block);
}

void TrackOutput::PlaceBlock(Block *block, rational start)
{
  AddBlockToGraph(block);

  if (block->in() == start) {
    return;
  }

  // Place block at the beginning
  if (start == 0) {
    // FIXME: Remove existing

    PrependBlock(block);
    return;
  }

  // Check if the placement location is past the end of the timeline
  if (start >= in()) {
    if (start > in()) {
      // If so, insert a gap here
      GapBlock* gap = new GapBlock();
      gap->set_length(start - in());

      // Then append them
      AppendBlock(gap);
    }

    AppendBlock(block);

    return;
  }

  // Check if the Block is placed at the in point of an existing Block, in which case a simple insert between will
  // suffice
  for (int i=1;i<block_cache_.size();i++) {
    Block* comparison = block_cache_.at(i);

    if (comparison->in() == start) {
      Block* previous = block_cache_.at(i-1);

      // InsertBlockAtIndex() could work here, but this function is faster since we've already found the Blocks
      InsertBlockBetweenBlocks(block, previous, comparison);
      return;
    }
  }
}

void TrackOutput::RemoveBlock(Block *block)
{
  GapBlock* gap = new GapBlock();
  gap->set_length(block->length());

  Block* previous = block->previous();
  Block* next = block->next();

  // Remove block
  RippleRemoveBlock(block);

  if (previous == nullptr) {
    // Block must be at the beginning
    PrependBlock(gap);
  } else {
    InsertBlockBetweenBlocks(gap, previous, next);
  }
}

void TrackOutput::RippleRemoveBlock(Block *block)
{
  Block* previous = block->previous();
  Block* next = block->next();

  if (previous != nullptr) {
    Block::DisconnectBlocks(previous, block);
  }

  if (next != nullptr) {
    Block::DisconnectBlocks(block, next);
  }

  if (previous != nullptr && next != nullptr) {
    Block::ConnectBlocks(previous, next);
  }
}

void TrackOutput::SplitBlock(Block *block, rational time)
{
  if (time < block->in() || time >= block->out()) {
    return;
  }

  rational original_length = block->length();

  block->set_length(time - block->in());

  Block* copy = block->copy();
  copy->set_length(original_length - block->length());
  InsertBlockAfter(copy, block);
}

void TrackOutput::SpliceBlock(Block *inner, Block *outer, rational inner_in)
{
  Q_ASSERT(inner_in >= outer->in() && inner_in < outer->out());

  // Cache original length
  rational original_length = outer->length();

  // Set outer clip to the clip that PRECEDES the inner clip
  outer->set_length(inner_in - outer->in());

  // Insert inner clip between BEFORE clip and its next clip
  InsertBlockAfter(inner, outer);

  // Create the AFTER clip
  Block* copy = outer->copy();
  copy->set_length(original_length - outer->length() - inner->length());
  InsertBlockAfter(copy, inner);
}