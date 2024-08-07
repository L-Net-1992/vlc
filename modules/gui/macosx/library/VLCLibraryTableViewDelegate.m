/*****************************************************************************
 * VLCLibraryTableViewDelegate.m: MacOS X interface module
 *****************************************************************************
 * Copyright (C) 2023 VLC authors and VideoLAN
 *
 * Authors: Claudio Cambra <developer@claudiocambra.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#import "VLCLibraryTableViewDelegate.h"

#import "library/VLCLibraryTableCellView.h"
#import "library/VLCLibraryTableCellViewProtocol.h"
#import "library/VLCLibraryTableViewDataSource.h"
#import "library/VLCLibraryDataTypes.h"
#import "library/VLCLibraryRepresentedItem.h"

@implementation VLCLibraryTableViewDelegate

- (instancetype)init
{
    self = [super init];
    if (self) {
        self.cellViewIdentifier = @"VLCLibraryTableViewCellIdentifier";
        self.cellViewClass = [VLCLibraryTableCellView class];
    }
    return self;
}

- (NSView *)tableView:(NSTableView *)tableView viewForTableColumn:(NSTableColumn *)tableColumn row:(NSInteger)row
{
    if (![tableView.dataSource conformsToProtocol:@protocol(VLCLibraryTableViewDataSource)]) {
        return nil;
    }

    NSObject<VLCLibraryTableViewDataSource> * const vlcDataSource = (NSObject<VLCLibraryTableViewDataSource>*)tableView.dataSource;
    NSAssert(vlcDataSource != nil, @"Should be a valid data source");

    NSView<VLCLibraryTableCellViewProtocol> * cellView = (NSView<VLCLibraryTableCellViewProtocol> *)[tableView makeViewWithIdentifier:self.cellViewIdentifier owner:self];

    if (cellView == nil && [self.cellViewClass respondsToSelector:@selector(fromNibWithOwner:)]) {
        cellView = [self.cellViewClass fromNibWithOwner:self];
        cellView.identifier = self.cellViewIdentifier;
    }

    NSObject<VLCMediaLibraryItemProtocol> * const libraryItem = [vlcDataSource libraryItemAtRow:row forTableView:tableView];
    if (libraryItem != nil) {
        VLCLibraryRepresentedItem * const representedItem = 
            [[VLCLibraryRepresentedItem alloc] initWithItem:libraryItem
                                                 parentType:vlcDataSource.currentParentType];
        [cellView setRepresentedItem:representedItem];
    }
    return cellView;
}

@end
